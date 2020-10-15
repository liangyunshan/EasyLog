# EasyLog

#日志记录时间日期
  日志模块中使用ftime/gettimeofday方法获取当前日期时间，经过测试这两个方法使用时消耗资源比较多。
  因此在模块内部记录日志时，比对前后两条日志的时间日期，当前后日志的时间日期相同且不足20次，则不获取并更新日志记录时间日期；
  当相同次数达到20次，则调用ftime/gettimeofday方法获取并更新当前日期时间。

#日志输出方式
  日志模块中通过设置环境变量MQTT_C_CLIENT_TRACE的值，可以将日志信息输出到不同的通道中：
  如果设置为ON，则输出到标准输出（stdout）中；
  如果设置为文件名，则将日志内容保存到文件中。如果日志行数超过预定的最大行数，则将当前日志文件备份（将原有文件名加上.tmp重命名），并创建新的文件保存日志;
  日志模块中默认的文件保存最大行数为1000行；
  日志模块中可以通过设置环境变量MQTT_C_CLIENT_TRACE_MAX_LINES的值，当设置的值不大于0，则仍然使用默认值。

#日志输出等级
  日志模块中可以通过环境变量MQTT_C_CLIENT_TRACE_LEVEL的值，过滤可输出的日志等级，设置的值包括下列字符串类型的值：
    MAXIMUM/TRACE_MAXIMUM	->1
    MEDIUM/TRACE_MEDIUM	->2
    MINIMUM/TRACE_MINIMUM	->3
    PROTOCOL/TRACE_PROTOCOL	->4
    ERROR/TRACE_ERROR		->5

    typedef struct
    {
      enum LOG_LEVELS trace_level;
      int max_trace_entries;	
      enum LOG_LEVELS trace_output_level;
    } trace_settings_type;

    trace_settings为trace_settings_type类型的值，初始化为：
    trace_settings_type trace_settings =
    {
      TRACE_MINIMUM,
      400,
      INVALID_LEVEL
    };

  当设置环境变量MQTT_C_CLIENT_TRACE_LEVEL的值为PROTOCOL/TRACE_PROTOCOL或者ERROR/TRACE_ERROR时候，
trace_output_level为相应的值，默认值为-1。
  总结来说，当将环境变量MQTT_C_CLIENT_TRACE_LEVEL的值设置为PROTOCOL/TRACE_PROTOCOL或者ERROR/TRACE_ERROR时，
除了不能输出日志等级小于trace_output_level的日志也就是MQTT_C_CLIENT_TRACE_LEVEL环境变量对应的日志等级；
当将环境变量MQTT_C_CLIENT_TRACE_LEVEL的值设置为MAXIMUM/TRACE_MAXIMUM或者MEDIUM/TRACE_MEDIUM或者MINIMUM/TRACE_MINIMUM时，
不能输出日志等级小于MQTT_C_CLIENT_TRACE_LEVEL的值对应的等级的日志。
