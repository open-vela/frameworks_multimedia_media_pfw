
# **PFW（Parameter-Framework）**

[English|[简体中文](./README_zh-cn.md)]

## **Overview**

Media Policy uses PFW to **construct routes**, **audio policies**, and various other states. PFW is a generic framework based on configuration files and callback functions to construct state machines.
- In the PFW configuration file, we define the states in the state machine, the conditions under which each state is valid, and the variables used for evaluation.
- The callback functions constitute the actions of the state machine.

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

The PFW module primarily includes features such as creating a system, modifying variables, and more.
 - **Creatie System**：Provides the path to the configuration file and plugins to create a PFW system.By implementing on_load and on_save methods, the PFW system can have the capability to read and save configurations immediately.
 - **Modify Variables**：Methods such as pfw_setint provide an interface to modify the value of a single variable.
 - **Query Variables**：Query the value of a single variable or print the entire state of the system through dump.
 - **Apply Changes**： Apply the current variable value to the state machine. If there is a change, the corresponding plugin will be called according to the logic in the configuration file.
 - **Subscribe to plugins**:We can subscribe to a specified plugin by name. In practice, a callback will be registered in the plugin. When the corresponding plugin is called, the previously registered callback will also be called to notify the subscriber

## **Write PFW configuration file**

### **Configuration File Components**
 - criteria.txt：Define all the variables in the system and the dependencies between them.
 - settings.pfw：Define the rules in the system, and the transfer conditions between states.

### **Criteria Syntax**

  In the criteria.txt file, each line defines a variable. The right side of := and |= indicates possible syntax expansions. The format is as follows:
```
<TYPE> <NAMES>:<RANGES>
```

**TYPE** :
Variables are essentially all of type int32_t, but their values can be interpreted in three types:
- **NumericalCriterion**：Numerical type, which is the raw int type. The value range is from INT32_MIN to INT32_MAX.
- **ExclusiveCriterion**：Enumerated type, similar to enums in C. A series of values starting from 0 are interpreted as meaningful strings.
- **InclusiveCriterion**：Mask type, where a series of bits from the least significant bit to the most significant bit are interpreted as meaningful strings.

**NAMES**:
Each variable can have one or more names, and these names are all bound to the same variable entity when reading or writing variables.

**RANGES**:
- The value domain of a **NumericalCriterion** is represented by one or more open intervals.
- The value domain of an **ExclusiveCriterion** and an **InclusiveCriterion** is represented by one or more strings.

#### **Example of writing a Criteria file**

 Take Audio policy as an example：
 ```shell
NumericalCriterion MediaVolume MusicVolume : [0,10] = 5
ExclusiveCriterion AudioMode               : normal phone
InclusiveCriterion AvailableDevices        : mic sco a2dp = mic
 ```
- **Media Volume**: numeric, value range is 0 to 10; initial value is 5.
- **Audio mode**: enumerated, value 0 corresponds to normal mode, value 1 corresponds to phone mode; initial value is 0 by default.
- **Available devices**: mask type, value 0 is the empty set, value 1 means mic, value 2 means sco, value 3 means. mic|sco, ......, value 7 means mic|sco|a2dp; initially turns on mic.

### **Settings.pfw Syntax**

The settings file consists of a number of **domain**, each representing a state machine. This configuration file distinguishes the hierarchy by indentation, with each tab or 4 spaces representing one level of indentation, and tabs and spaces cannot be mixed (same as python).
- Each state machine has several confs, each representing a state.
- Each state has two parts, **condition** and **action**. When we apply a criterion, we traverse all the states in order, and we enter the state that satisfies the **condition** first. We will enter the state that satisfies the **condition** first, and if we enter a new state, we will execute its **action**.
 ```shell
  domain: string
    conf: string
        <RULES>
        <ACTS>
```
- **RULES** section defines constraints on the values of criteria. These constraints can be recursively combined using "and" and "or" logical operators. Different types of criteria use different predicates for their constraints.
- **ACTS** represent the execution of one or more plugin callbacks. In the ACTS section, if you want to reference the current value of a criterion variable rather than a fixed value, the following syntax is supported:
```shell
param%criterion%param
```
#### **Example of writing a Settings file**

As an example of Audio sco node control, when sco is available and the user needs it, the sample rate is updated via the FFmpegCommand plugin and the sco input/output nodes are turned:
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

In the pfw/test directory, we have configured a Makefile for use on the host. This Makefile can compile and generate a test command-line tool. You can use this tool to input commands and debug the two .conf configuration files in the same folder. Additionally, when developing new features for PFW, this tool can be used to quickly check the code.
```shell
pfw/test$ make cc -o test ../system.c ../parser.c ../context.c ../vector.c ../sanitizer.c ../criterion.c test.c -Wall -Werror -O0 -g -I ../include -D CONFIG_LIB_PFW_DEBUG -fsanitize=address -fsanitize=leak
pfw/test$ ./test

pfw> dump         //Print dump information.
pfw> setint persist.media.MediaVolume 7     //Set the MediaVolume value to 7.
```