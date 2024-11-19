
# **PFW（Parameter-Framework）**

[English | [简体中文](./README_zh-cn.md)]

## **Overview**

`Media Policy` uses `PFW` to **construct various states such as routing** and **audio policy**. `PFW` is a general framework for constructing state machines based on configuration files and callback functions.
- In the `PFW` configuration file, we will define which states are in the state machine, the conditions for each state to be established, and the variables used for judgment.
- The callback function constitutes the action of the state machine.

![pfw 框架图](../images/pfw/pfw.jpg)

## **Project Directory**
```tree
.
├── context.c
├── criterion.c
├── dump.c
├── include
│   └── pfw.h
├── internal.h
├── Kconfig
├── Make.defs
├── Makefile
├── parser.c
├── README.md
├── README_zh-cn.md
├── sanitizer.c
├── system.c
├── test
│   ├── criteria.txt
│   ├── Makefile
│   ├── settings.pfw
│   └── test.c
└── vector.c
```

## **Function Introduction**

The `PFW` module mainly includes functions such as creating a system and modifying variables.
- **Create a system**: Provide a configuration file path and plugin to create a `pfw` system. By implementing the `on_load/on_save` method, the `PFW` system can have the functions of reading and instant saving.
- **Modify variables**: `pfw_setint` and other methods provide an interface for modifying the value of a single variable.
- **Query variables**: Query the value of a single variable, or print the status of the entire system through `dump`.
- **Apply changes**: Apply the current variable value to the state machine. If a change occurs, the corresponding plugin will be called according to the logic in the configuration file.
- **Subscribe to plugin**: Subscribe to the specified plugin by name, register a `callback` to the plugin, so that when the corresponding plugin is called, the previously registered `callback` will also be called to notify the subscriber.

## **Write PFW configuration file**

### **Configuration file composition**
The `PFW` configuration file consists of two files: `criteria.txt` and `settings.pfw`:
- `criteria.txt`: defines all variables in the system and the dependencies between variables.
- `settings.pfw`: defines the rules in the system and the transition conditions between states.


### **Criteria Syntax**

In the `criteria.txt` file, each line defines a variable, and the right side of each line of `:=` and `|=` represents a possible syntax expansion, in the format of:

```
<TYPE> <NAMES>:<RANGES>
```

**Type** variables are essentially `int32_t`, but the values ​​can be interpreted as three types:
- **NumericalCriterion**: Numeric type, that is, the most primitive `int` type, with a value range of `INT32_MIN` to `INT32_MAX`.
- **ExclusiveCriterion**: Enumeration type, similar to `enum` in `C` language, several values ​​starting from `0` are interpreted as meaningful strings.
- **InclusiveCriterion**: Mask type, several `bits` from low to high are interpreted as meaningful strings.

**NAMES**: Each variable can have one or more names, and these names are bound to the same variable entity when reading and writing variables.

**RANGES**:
- The value range of **NumericalCriterion** is represented by one or more open intervals.
- The value ranges of **ExclusiveCriterion** and **InclusiveCriterion** are represented by one or more strings.


#### **Example of writing a Criteria file**

 Take `Audio policy` as an example:

 ```shell
NumericalCriterion MediaVolume MusicVolume : [0,10] = 5
ExclusiveCriterion AudioMode               : normal phone
InclusiveCriterion AvailableDevices        : mic sco a2dp = mic
 ```
- **Media Volume**: numeric, value range is 0 to 10; initial value is 5.
- **Audio mode**: enumerated, value 0 corresponds to normal mode, value 1 corresponds to phone mode; initial value is 0 by default.
- **Available devices**: mask type, value 0 is the empty set, value 1 means `mic`, value 2 means `sco`, value 3 means `mic|sco`, ......, value 7 means `mic|sco|a2dp`; initially turns on mic.

### **Settings.pfw Syntax**

The settings file consists of several **domain**, each domain represents a state machine. This configuration file uses indentation to distinguish the hierarchy. Each `tab` or 4 spaces represents a level of indentation, and `tab` and spaces cannot be mixed (the same as python).


- Each state machine has several confs, each representing a state.
- Each state has two parts, **condition** and **action**. When we apply a criterion, we traverse all the states in order, and we enter the state that satisfies the **condition** first. We will enter the state that satisfies the **condition** first, and if we enter a new state, we will execute its **action**.
    ```shell
    domain: string
        conf: string
            <RULES>
            <ACTS>
    ```
- The **RULES** part is the constraints on the criterion values, which can be recursively combined through **AND** and **OR**; different types of `criterion` constraints use different predicates.


- **ACTS** is the execution of one or more plugin callbacks. In ACTS, if you want to refer to the current value of the criterion variable instead of a fixed value, we support the following syntax:
    ```shell
    param%criterion%param
    ```
#### **Example of writing a Settings file**

Taking the `Audio sco` node control as an example, when `sco` is available and the user needs it, the sampling rate will be updated through the `FFmpegCommand` plug-in, and the `sco` input and output nodes will be opened:

```shell
domain: SCO
    conf: enable
        ALL
            UsingDevices Includes sco
            AvailableDevices Includes sco
        FFmpegCommand = sco,sample_rate,%HFPSampleRate%;sco,play,;
    conf: disable
        ALL
        FFmpegCommand = sco,pause,;
```

## **Check Tool**

In the `pfw/test` directory, we have configured a `Makefile` for use on the `host`, which can compile and generate the `test` command line tool, in which you can enter commands to debug the two `.conf` configuration files in the same folder. At the same time, when developing new `pfw` features, you can also use this tool to quickly check the code.

```shell
pfw/test$ make cc -o test ../system.c ../parser.c ../context.c ../vector.c ../sanitizer.c ../criterion.c test.c -Wall -Werror -O0 -g -I ../include -D CONFIG_LIB_PFW_DEBUG -fsanitize=address -fsanitize=leak
pfw/test$ ./test

pfw> dump         //Print dump information.
pfw> setint persist.media.MediaVolume 7     //Set the MediaVolume value to 7.
```