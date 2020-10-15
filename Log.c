/*******************************************************************************
 * Copyright (c) 2009, 2018 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    https://www.eclipse.org/legal/epl-2.0/
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *    Ian Craggs - updates for the async client
 *    Ian Craggs - fix for bug #427028
 *******************************************************************************/

/**
 * @file
 * \brief Logging and tracing module
 *
 *
 */

#include "Log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

#if !defined(_WIN32) && !defined(_WIN64)
#include <syslog.h>
#include <sys/stat.h>
#include<unistd.h>
#define GETTIMEOFDAY 1
#else
#define snprintf _snprintf
#endif

#if defined(GETTIMEOFDAY)
	#include <sys/time.h>
#else
	#include <sys/timeb.h>
#endif

#if !defined(_WIN32) && !defined(_WIN64)
/**
 * _unlink mapping for linux
 */
#define _unlink unlink
#endif


#if !defined(min)
#define min(A,B) ( (A) < (B) ? (A):(B))
#endif

trace_settings_type trace_settings =
{
	TRACE_MINIMUM,
	400,
	INVALID_LEVEL
};

#define MAX_FUNCTION_NAME_LENGTH 256

typedef struct
{
#if defined(GETTIMEOFDAY)
	struct timeval ts;
#else
	struct timeb ts;
#endif
	int sametime_count;
	int number;
	int thread_id;
	int depth;
	char name[MAX_FUNCTION_NAME_LENGTH + 1];
	int line;
	int has_rc;
	int rc;
	enum LOG_LEVELS level;
} traceEntry;

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define mutex_type HANDLE
#else
    #include <pthread.h>
    #define mutex_type pthread_mutex_t*
#endif

#if !defined(SOCKET_ERROR)
    /** error in socket operation */
    #define SOCKET_ERROR -1
#endif

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define thread_type HANDLE
    #define thread_id_type DWORD
    #define thread_return_type DWORD
    #define thread_fn LPTHREAD_START_ROUTINE
    #define cond_type HANDLE
    #define sem_type HANDLE
    #undef ETIMEDOUT
    #define ETIMEDOUT WSAETIMEDOUT
#else
    #include <pthread.h>

    #define thread_type pthread_t
    #define thread_id_type pthread_t
    #define thread_return_type void*
    typedef thread_return_type (*thread_fn)(void*);
    typedef struct { pthread_cond_t cond; pthread_mutex_t mutex; } cond_type_struct;
    typedef cond_type_struct *cond_type;
    #if defined(OSX)
      #include <dispatch/dispatch.h>
      typedef dispatch_semaphore_t sem_type;
    #else
      #include <semaphore.h>
      typedef sem_t *sem_type;
    #endif

    cond_type Thread_create_cond(int*);
    int Thread_signal_cond(cond_type);
    int Thread_wait_cond(cond_type condvar, int timeout);
    int Thread_destroy_cond(cond_type);
#endif

/**
 * Start a new thread
 * @param fn the function to run, must be of the correct signature
 * @param parameter pointer to the function parameter, can be NULL
 * @return the new thread
 */
thread_type Thread_start(thread_fn fn, void* parameter)
{
#if defined(_WIN32) || defined(_WIN64)
    thread_type thread = NULL;
#else
    thread_type thread = 0;
    pthread_attr_t attr;
#endif

#if defined(_WIN32) || defined(_WIN64)
    thread = CreateThread(NULL, 0, fn, parameter, 0, NULL);
#else
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, fn, parameter) != 0)
        thread = 0;
    pthread_attr_destroy(&attr);
#endif
    return thread;
}


/**
 * Create a new mutex
 * @return the new mutex
 */
mutex_type Thread_create_mutex(int* rc)
{
    mutex_type mutex = NULL;

    *rc = -1;
    #if defined(_WIN32) || defined(_WIN64)
        mutex = CreateMutex(NULL, 0, NULL);
        if (mutex == NULL)
            *rc = GetLastError();
    #else
        mutex = malloc(sizeof(pthread_mutex_t));
        if (mutex)
            *rc = pthread_mutex_init(mutex, NULL);
    #endif
    return mutex;
}


/**
 * Lock a mutex which has alrea
 * @return completion code, 0 is success
 */
int Thread_lock_mutex(mutex_type mutex)
{
    int rc = -1;

    /* don't add entry/exit trace points as the stack log uses mutexes - recursion beckons */
    #if defined(_WIN32) || defined(_WIN64)
        /* WaitForSingleObject returns WAIT_OBJECT_0 (0), on success */
        rc = WaitForSingleObject(mutex, INFINITE);
    #else
        rc = pthread_mutex_lock(mutex);
    #endif

    return rc;
}


/**
 * Unlock a mutex which has already been locked
 * @param mutex the mutex
 * @return completion code, 0 is success
 */
int Thread_unlock_mutex(mutex_type mutex)
{
    int rc = -1;

    /* don't add entry/exit trace points as the stack log uses mutexes - recursion beckons */
    #if defined(_WIN32) || defined(_WIN64)
        /* if ReleaseMutex fails, the return value is 0 */
        if (ReleaseMutex(mutex) == 0)
            rc = GetLastError();
        else
            rc = 0;
    #else
        rc = pthread_mutex_unlock(mutex);
    #endif

    return rc;
}


/**
 * Destroy a mutex which has already been created
 * @param mutex the mutex
 */
int Thread_destroy_mutex(mutex_type mutex)
{
    int rc = 0;

    #if defined(_WIN32) || defined(_WIN64)
        rc = CloseHandle(mutex);
    #else
        rc = pthread_mutex_destroy(mutex);
        free(mutex);
    #endif
    return rc;
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define max_msg_len 120

static const char *protocol_message_list[] =
{
    "%d %s -> CONNECT version %d clean: %d (%d)", /* 0, was 131, 68 and 69 */
    "%d %s <- CONNACK rc: %d", /* 1, was 132 */
    "%d %s -> CONNACK rc: %d (%d)", /* 2, was 138 */
    "%d %s <- PINGREQ", /* 3, was 35 */
    "%d %s -> PINGRESP (%d)", /* 4 */
    "%d %s <- DISCONNECT", /* 5 */
    "%d %s <- SUBSCRIBE msgid: %d", /* 6, was 39 */
    "%d %s -> SUBACK msgid: %d (%d)", /* 7, was 40 */
    "%d %s <- UNSUBSCRIBE msgid: %d", /* 8, was 41 */
    "%d %s -> UNSUBACK msgid: %d (%d)", /* 9 */
    "%d %s -> PUBLISH msgid: %d qos: %d retained: %d rc %d payload len(%d): %.*s", /* 10, was 42 */
    "%d %s <- PUBLISH msgid: %d qos: %d retained: %d payload len(%d): %.*s", /* 11, was 46 */
    "%d %s -> PUBACK msgid: %d (%d)", /* 12, was 47 */
    "%d %s -> PUBREC msgid: %d (%d)", /* 13, was 48 */
    "%d %s <- PUBACK msgid: %d", /* 14, was 49 */
    "%d %s <- PUBREC msgid: %d", /* 15, was 53 */
    "%d %s -> PUBREL msgid: %d (%d)", /* 16, was 57 */
    "%d %s <- PUBREL msgid %d", /* 17, was 58 */
    "%d %s -> PUBCOMP msgid %d (%d)", /* 18, was 62 */
    "%d %s <- PUBCOMP msgid:%d", /* 19, was 63 */
    "%d %s -> PINGREQ (%d)", /* 20, was 137 */
    "%d %s <- PINGRESP", /* 21, was 70 */
    "%d %s -> SUBSCRIBE msgid: %d (%d)", /* 22, was 72 */
    "%d %s <- SUBACK msgid: %d", /* 23, was 73 */
    "%d %s <- UNSUBACK msgid: %d", /* 24, was 74 */
    "%d %s -> UNSUBSCRIBE msgid: %d (%d)", /* 25, was 106 */
    "%d %s <- CONNECT", /* 26 */
    "%d %s -> PUBLISH qos: 0 retained: %d rc: %d payload len(%d): %.*s", /* 27 */
    "%d %s -> DISCONNECT (%d)", /* 28 */
    "Socket error for client identifier %s, socket %d, peer address %s; ending connection", /* 29 */
};

static const char *trace_message_list[] =
{
    "Failed to remove client from bstate->clients", /* 0 */
    "Removed client %s from bstate->clients, socket %d", /* 1 */
    "Packet_Factory: unhandled packet type %d", /* 2 */
    "Packet %s received from client %s for message identifier %d, but no record of that message identifier found", /* 3 */
    "Packet %s received from client %s for message identifier %d, but message is wrong QoS, %d", /* 4 */
    "Packet %s received from client %s for message identifier %d, but message is in wrong state", /* 5 */
    "%s received from client %s for message id %d - removing publication", /* 6 */
    "Trying %s again for client %s, socket %d, message identifier %d", /* 7 */
    "", /* 8 */
    "(%lu) %*s(%d)> %s:%d", /* 9 */
    "(%lu) %*s(%d)< %s:%d", /* 10 */
    "(%lu) %*s(%d)< %s:%d (%d)", /* 11 */
    "Storing unsent QoS 0 message", /* 12 */
};

/**
 * Get a log message by its index
 * @param index the integer index
 * @param log_level the log level, used to determine which message list to use
 * @return the message format string
 */
const char* Messages_get(int index, enum LOG_LEVELS log_level)
{
    const char *msg = NULL;

    if (log_level == TRACE_PROTOCOL)
        msg = (index >= 0 && index < ARRAY_SIZE(protocol_message_list)) ? protocol_message_list[index] : NULL;
    else
        msg = (index >= 0 && index < ARRAY_SIZE(trace_message_list)) ? trace_message_list[index] : NULL;
    return msg;
}

static int start_index = -1,
			next_index = 0;
static traceEntry* trace_queue = NULL;
static int trace_queue_size = 0;

static FILE* trace_destination = NULL;	/**< flag to indicate if trace is to be sent to a stream */
static char* trace_destination_name = NULL; /**< the name of the trace file */
static char* trace_destination_backup_name = NULL; /**< the name of the backup trace file */
static int lines_written = 0; /**< number of lines written to the current output file */
static int max_lines_per_file = 1000; /**< maximum number of lines to write to one trace file */
static enum LOG_LEVELS trace_output_level = INVALID_LEVEL;
static Log_traceCallback* trace_callback = NULL;
static traceEntry* Log_pretrace(void);
static char* Log_formatTraceEntry(traceEntry* cur_entry);
static void Log_output(enum LOG_LEVELS log_level, const char *msg);
static void Log_posttrace(enum LOG_LEVELS log_level, traceEntry* cur_entry);
static void Log_trace(enum LOG_LEVELS log_level, const char *buf);
#if 0
static FILE* Log_destToFile(const char *dest);
static int Log_compareEntries(const char *entry1, const char *entry2);
#endif

static int sametime_count = 0;
#if defined(GETTIMEOFDAY)
struct timeval now_ts, last_ts;
#else
struct timeb now_ts, last_ts;
#endif
static char msg_buf[512];

#if defined(_WIN32) || defined(_WIN64)
mutex_type log_mutex;
#else
static pthread_mutex_t log_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type log_mutex = &log_mutex_store;
#endif


int Log_initialize(Log_nameValue* info)
{
	int rc = SOCKET_ERROR;
	char* envval = NULL;
#if !defined(_WIN32) && !defined(_WIN64)
	struct stat buf;
#endif

	if ((trace_queue = malloc(sizeof(traceEntry) * trace_settings.max_trace_entries)) == NULL)
		goto exit;
	trace_queue_size = trace_settings.max_trace_entries;

	if ((envval = getenv("MQTT_C_CLIENT_TRACE")) != NULL && strlen(envval) > 0)
	{
		if (strcmp(envval, "ON") == 0 || (trace_destination = fopen(envval, "w")) == NULL)
			trace_destination = stdout;
		else
		{
			if ((trace_destination_name = malloc(strlen(envval) + 1)) == NULL)
			{
				free(trace_queue);
				goto exit;
			}
			strcpy(trace_destination_name, envval);
			if ((trace_destination_backup_name = malloc(strlen(envval) + 3)) == NULL)
			{
				free(trace_queue);
				free(trace_destination_name);
				goto exit;
			}
            sprintf(trace_destination_backup_name, "%s.tmp", trace_destination_name);
		}
	}
	if ((envval = getenv("MQTT_C_CLIENT_TRACE_MAX_LINES")) != NULL && strlen(envval) > 0)
	{
		max_lines_per_file = atoi(envval);
		if (max_lines_per_file <= 0)
			max_lines_per_file = 1000;
	}
	if ((envval = getenv("MQTT_C_CLIENT_TRACE_LEVEL")) != NULL && strlen(envval) > 0)
	{
		if (strcmp(envval, "MAXIMUM") == 0 || strcmp(envval, "TRACE_MAXIMUM") == 0)
            trace_settings.trace_level  = TRACE_MAXIMUM;
		else if (strcmp(envval, "MEDIUM") == 0 || strcmp(envval, "TRACE_MEDIUM") == 0)
			trace_settings.trace_level = TRACE_MEDIUM;
		else if (strcmp(envval, "MINIMUM") == 0 || strcmp(envval, "TRACE_MINIMUM") == 0)
			trace_settings.trace_level = TRACE_MINIMUM;
		else if (strcmp(envval, "PROTOCOL") == 0  || strcmp(envval, "TRACE_PROTOCOL") == 0)
			trace_output_level = TRACE_PROTOCOL;
		else if (strcmp(envval, "ERROR") == 0  || strcmp(envval, "TRACE_ERROR") == 0)
			trace_output_level = LOG_ERROR;
	}
	Log_output(TRACE_MINIMUM, "=========================================================");
	Log_output(TRACE_MINIMUM, "                   Trace Output");
	if (info)
	{
		while (info->name)
		{
			snprintf(msg_buf, sizeof(msg_buf), "%s: %s", info->name, info->value);
			Log_output(TRACE_MINIMUM, msg_buf);
			info++;
		}
	}
#if !defined(_WIN32) && !defined(_WIN64)
	if (stat("/proc/version", &buf) != -1)
	{
		FILE* vfile;

		if ((vfile = fopen("/proc/version", "r")) != NULL)
		{
			int len;

			strcpy(msg_buf, "/proc/version: ");
			len = strlen(msg_buf);
			if (fgets(&msg_buf[len], sizeof(msg_buf) - len, vfile))
				Log_output(TRACE_MINIMUM, msg_buf);
			fclose(vfile);
		}
	}
#endif
#if defined(GETTIMEOFDAY)
        gettimeofday(&now_ts, NULL);
#else
        ftime(&now_ts);
#endif
	Log_output(TRACE_MINIMUM, "=========================================================");
exit:
	return rc;
}


void Log_setTraceCallback(Log_traceCallback* callback)
{
	trace_callback = callback;
}


void Log_setTraceLevel(enum LOG_LEVELS level)
{
	if (level < TRACE_MINIMUM) /* the lowest we can go is TRACE_MINIMUM*/
		trace_settings.trace_level = level;
	trace_output_level = level;
}


void Log_terminate(void)
{
	free(trace_queue);
	trace_queue = NULL;
	trace_queue_size = 0;
	if (trace_destination)
	{
		if (trace_destination != stdout)
			fclose(trace_destination);
		trace_destination = NULL;
	}
	if (trace_destination_name) {
		free(trace_destination_name);
		trace_destination_name = NULL;
	}
	if (trace_destination_backup_name) {
		free(trace_destination_backup_name);
		trace_destination_backup_name = NULL;
	}
	start_index = -1;
	next_index = 0;
	trace_output_level = INVALID_LEVEL;
	sametime_count = 0;
}


static traceEntry* Log_pretrace(void)
{
	traceEntry *cur_entry = NULL;

	/* calling ftime/gettimeofday seems to be comparatively expensive, so we need to limit its use */
	if (++sametime_count % 20 == 0)
	{
#if defined(GETTIMEOFDAY)
		gettimeofday(&now_ts, NULL);
		if (now_ts.tv_sec != last_ts.tv_sec || now_ts.tv_usec != last_ts.tv_usec)
#else
		ftime(&now_ts);
		if (now_ts.time != last_ts.time || now_ts.millitm != last_ts.millitm)
#endif
		{
			sametime_count = 0;
			last_ts = now_ts;
		}
	}

	if (trace_queue_size != trace_settings.max_trace_entries)
	{
		traceEntry* new_trace_queue = malloc(sizeof(traceEntry) * trace_settings.max_trace_entries);

		if (new_trace_queue == NULL)
			goto exit;
		memcpy(new_trace_queue, trace_queue, min(trace_queue_size, trace_settings.max_trace_entries) * sizeof(traceEntry));
		free(trace_queue);
		trace_queue = new_trace_queue;
		trace_queue_size = trace_settings.max_trace_entries;

		if (start_index > trace_settings.max_trace_entries + 1 ||
				next_index > trace_settings.max_trace_entries + 1)
		{
			start_index = -1;
			next_index = 0;
		}
	}

	/* add to trace buffer */
	cur_entry = &trace_queue[next_index];
	if (next_index == start_index) /* means the buffer is full */
	{
		if (++start_index == trace_settings.max_trace_entries)
			start_index = 0;
	} else if (start_index == -1)
		start_index = 0;
	if (++next_index == trace_settings.max_trace_entries)
		next_index = 0;
exit:
	return cur_entry;
}

static char* Log_formatTraceEntry(traceEntry* cur_entry)
{
	struct tm *timeinfo;
	int buf_pos = 31;

#if defined(GETTIMEOFDAY)
	timeinfo = localtime((time_t *)&cur_entry->ts.tv_sec);
#else
	timeinfo = localtime(&cur_entry->ts.time);
#endif
    strftime(&msg_buf[7], 80, "%Y-%m-%d %H:%M:%S. ", timeinfo);
#if defined(GETTIMEOFDAY)
    sprintf(&msg_buf[27], "%.3lu ", cur_entry->ts.tv_usec / 1000L);
#else
    sprintf(&msg_buf[27], ".%.3hu ", cur_entry->ts.millitm);
#endif
    buf_pos = 31;

	sprintf(msg_buf, "(%.4d)", cur_entry->sametime_count);
	msg_buf[6] = ' ';


	if (cur_entry->has_rc == 2)
		strncpy(&msg_buf[buf_pos], cur_entry->name, sizeof(msg_buf)-buf_pos);
	else
	{
		const char *format = Messages_get(cur_entry->number, cur_entry->level);
		if (cur_entry->has_rc == 1)
			snprintf(&msg_buf[buf_pos], sizeof(msg_buf)-buf_pos, format, cur_entry->thread_id,
					cur_entry->depth, "", cur_entry->depth, cur_entry->name, cur_entry->line, cur_entry->rc);
		else
			snprintf(&msg_buf[buf_pos], sizeof(msg_buf)-buf_pos, format, cur_entry->thread_id,
					cur_entry->depth, "", cur_entry->depth, cur_entry->name, cur_entry->line);
	}
	return msg_buf;
}


static void Log_output(enum LOG_LEVELS log_level, const char *msg)
{
	if (trace_destination)
	{
		fprintf(trace_destination, "%s\n", msg);

		if (trace_destination != stdout && ++lines_written >= max_lines_per_file)
		{

			fclose(trace_destination);
			_unlink(trace_destination_backup_name); /* remove any old backup trace file */
			rename(trace_destination_name, trace_destination_backup_name); /* rename recently closed to backup */
			trace_destination = fopen(trace_destination_name, "w"); /* open new trace file */
			if (trace_destination == NULL)
				trace_destination = stdout;
			lines_written = 0;
		}
		else
			fflush(trace_destination);
	}

	if (trace_callback)
		(*trace_callback)(log_level, msg);
}


static void Log_posttrace(enum LOG_LEVELS log_level, traceEntry* cur_entry)
{
	if (((trace_output_level == -1) ? log_level >= trace_settings.trace_level : log_level >= trace_output_level))
	{
		char* msg = NULL;

		if (trace_destination || trace_callback)
			msg = &Log_formatTraceEntry(cur_entry)[7];

		Log_output(log_level, msg);
	}
}


static void Log_trace(enum LOG_LEVELS log_level, const char *buf)
{
	traceEntry *cur_entry = NULL;

	if (trace_queue == NULL)
		return;

	cur_entry = Log_pretrace();

	memcpy(&(cur_entry->ts), &now_ts, sizeof(now_ts));

	cur_entry->sametime_count = sametime_count;

	cur_entry->has_rc = 2;
	strncpy(cur_entry->name, buf, sizeof(cur_entry->name));
	cur_entry->name[MAX_FUNCTION_NAME_LENGTH] = '\0';

	Log_posttrace(log_level, cur_entry);
}


/**
 * Log a message.  If possible, all messages should be indexed by message number, and
 * the use of the format string should be minimized or negated altogether.  If format is
 * provided, the message number is only used as a message label.
 * @param log_level the log level of the message
 * @param msgno the id of the message to use if the format string is NULL
 * @param aFormat the printf format string to be used if the message id does not exist
 * @param ... the printf inserts
 */
void Log(enum LOG_LEVELS log_level, int msgno, const char *format, ...)
{
	if (log_level >= trace_settings.trace_level)
	{
		const char *temp = NULL;
		va_list args;

		/* we're using a static character buffer, so we need to make sure only one thread uses it at a time */
		Thread_lock_mutex(log_mutex);
		if (format == NULL && (temp = Messages_get(msgno, log_level)) != NULL)
			format = temp;

		va_start(args, format);
		vsnprintf(msg_buf, sizeof(msg_buf), format, args);

		Log_trace(log_level, msg_buf);
		va_end(args);
		Thread_unlock_mutex(log_mutex);
	}
}


/**
 * The reason for this function is to make trace logging as fast as possible so that the
 * function exit/entry history can be captured by default without unduly impacting
 * performance.  Therefore it must do as little as possible.
 * @param log_level the log level of the message
 * @param msgno the id of the message to use if the format string is NULL
 * @param aFormat the printf format string to be used if the message id does not exist
 * @param ... the printf inserts
 */
void Log_stackTrace(enum LOG_LEVELS log_level, int msgno, int thread_id, int current_depth, const char* name, int line, int* rc)
{
	traceEntry *cur_entry = NULL;

	if (trace_queue == NULL)
		return;

	if (log_level < trace_settings.trace_level)
		return;

	Thread_lock_mutex(log_mutex);
	cur_entry = Log_pretrace();

	memcpy(&(cur_entry->ts), &now_ts, sizeof(now_ts));
	cur_entry->sametime_count = sametime_count;
	cur_entry->number = msgno;
	cur_entry->thread_id = thread_id;
	cur_entry->depth = current_depth;
	strcpy(cur_entry->name, name);
	cur_entry->level = log_level;
	cur_entry->line = line;
	if (rc == NULL)
		cur_entry->has_rc = 0;
	else
	{
		cur_entry->has_rc = 1;
		cur_entry->rc = *rc;
	}

	Log_posttrace(log_level, cur_entry);
	Thread_unlock_mutex(log_mutex);
}


#if 0
static FILE* Log_destToFile(const char *dest)
{
	FILE* file = NULL;

	if (strcmp(dest, "stdout") == 0)
		file = stdout;
	else if (strcmp(dest, "stderr") == 0)
		file = stderr;
	else
	{
		if (strstr(dest, "FFDC"))
			file = fopen(dest, "ab");
		else
			file = fopen(dest, "wb");
	}
	return file;
}


static int Log_compareEntries(const char *entry1, const char *entry2)
{
	int comp = strncmp(&entry1[7], &entry2[7], 19);

	/* if timestamps are equal, use the sequence numbers */
	if (comp == 0)
		comp = strncmp(&entry1[1], &entry2[1], 4);

	return comp;
}


/**
 * Write the contents of the stored trace to a stream
 * @param dest string which contains a file name or the special strings stdout or stderr
 */
int Log_dumpTrace(char* dest)
{
	FILE* file = NULL;
	ListElement* cur_trace_entry = NULL;
	const int msgstart = 7;
	int rc = -1;
	int trace_queue_index = 0;

	if ((file = Log_destToFile(dest)) == NULL)
	{
		Log(LOG_ERROR, 9, NULL, "trace", dest, "trace entries");
		goto exit;
	}

	fprintf(file, "=========== Start of trace dump ==========\n");
	/* Interleave the log and trace entries together appropriately */
	ListNextElement(trace_buffer, &cur_trace_entry);
	trace_queue_index = start_index;
	if (trace_queue_index == -1)
		trace_queue_index = next_index;
	else
	{
		Log_formatTraceEntry(&trace_queue[trace_queue_index++]);
		if (trace_queue_index == trace_settings.max_trace_entries)
			trace_queue_index = 0;
	}
	while (cur_trace_entry || trace_queue_index != next_index)
	{
		if (cur_trace_entry && trace_queue_index != -1)
		{	/* compare these timestamps */
			if (Log_compareEntries((char*)cur_trace_entry->content, msg_buf) > 0)
				cur_trace_entry = NULL;
		}

		if (cur_trace_entry)
		{
			fprintf(file, "%s\n", &((char*)(cur_trace_entry->content))[msgstart]);
			ListNextElement(trace_buffer, &cur_trace_entry);
		}
		else
		{
			fprintf(file, "%s\n", &msg_buf[7]);
			if (trace_queue_index != next_index)
			{
				Log_formatTraceEntry(&trace_queue[trace_queue_index++]);
				if (trace_queue_index == trace_settings.max_trace_entries)
					trace_queue_index = 0;
			}
		}
	}
	fprintf(file, "========== End of trace dump ==========\n\n");
	if (file != stdout && file != stderr && file != NULL)
		fclose(file);
	rc = 0;
exit:
	return rc;
}
#endif


