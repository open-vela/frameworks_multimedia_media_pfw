# **PFW（Parameter-Framework）**

[[English](./README.md) | 简体中文]

## **概述**

`Media Policy` 通过 `PFW` 来**构造路由**、**音频策略**等各种状态。`PFW` 是一个基于配置文件和回调函数来构造状态机的通用框架。
- 在 `PFW` 配置文件中，我们会定义状态机中有哪些状态，每个状态成立的条件，用于判断的变量。
- 回调函数则构成了状态机的动作。

![pfw 框架图](../images/pfw/pfw.jpg)

## **项目目录**

```tree
.
├── context.c
├── criterion.c
├── dump.c
├── include
│   └── pfw.h
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
│   ├── criteria.txt
│   ├── Makefile
│   ├── settings.pfw
│   └── test.c
└── vector.c
```
## **功能介绍**

`PFW` 模块主要包含创建系统，修改变量等功能。
 - **创建系统**：提供配置文件路径和插件来创建 `pfw` 系统，通过实现了 `on_load/on_save` 方法，可以让 `PFW` 系统具有读取和即时保存的功能。
 - **修改变量**：`pfw_setint` 等方法提供修改单个变量值的接口。
 - **查询变量**：查询单个变量的值，或者通过 `dump` 打印整个系统的状态。
 - **应用变化**：把当前的变量取值应用到状态机上，如果发生了变化，便会根据配置文件中的逻辑调用相应的插件。
 - **订阅插件**：通过名字订阅制定的插件，注册一个 `callback` 到插件中，这样在相应的插件被调用时，也会调用之前注册的 `callback`，从而通知到订阅者。

## **编写 PFW 配置文件**

### **配置文件组成**
`PFW` 配置文件由 `criteria.txt` 和 `settings.pfw` 两个文件组成：
 - `criteria.txt`：定义系统中的所有变量，以及变量之间的依赖关系。
 - `settings.pfw`：定义系统中的规则，以及状态之间的转移条件。

### **Criteria 语法**
  
`criteria.txt` 文件中，每一行定义一个变量，每行 `:=` 和 `|=` 右边表示一种语法展开的可能，格式为：
```
<类型> <变量名>:<值域>
```

**类型**变量的本质上都是 `int32_t`，但是取值会可以被解释为三种类型：
- **NumericalCriterion**：数值型，即最原始的 `int` 型，取值范围是 `INT32_MIN` 到 `INT32_MAX`。
- **ExclusiveCriterion**：枚举型，类似 `C` 语言的 `enum`，从 `0` 开始的若干个值被解释为有意义的字符串。
- **InclusiveCriterion**：掩码型，从低位到高位的若干个 `bit` 被解释为有意义的字符串。

**变量名**：每个变量可以有一个或更多的名字，在读写变量时这些名字都绑定到同一个变量实体。

**值域**：
- **NumericalCriterion** 的值域用一个或更多开区间表示。
- **ExclusiveCriterion** 和 **InclusiveCriterion** 的值域则用一个或更多的字符串表示。

#### **Criteria 文件编写示例**

 以 `Audio policy` 为例：
 ```shell
NumericalCriterion MediaVolume MusicVolume : [0,10] = 5
ExclusiveCriterion AudioMode               : normal phone
InclusiveCriterion AvailableDevices        : mic sco a2dp = mic
 ```
- **媒体音量**：数值型，取值范围是 0～10；初始值为 5。
- **音频模式**：枚举型，取值 0 对应 normal 模式，取值 1 对应 phone 模式；初始值默认为 0。
- **可用设备**：掩码型，取值 0 为空集，取值 1 表示 `mic`，取值 2 表示 `sco`，取值 3 表示 `mic|sco`，……，取值 7 表示 `mic|sco|a2dp`；初始便开启 `mic`。

### **Settings.pfw 语法**
settings 文件由若干个 **domain** 组成，每个 domain 都代表一个状态机，这个配置文件通过缩进来区分层次结构，每个 `tab` 或者4个空格表示一级缩进，且 `tab` 和空格不能混用（与 python 相同）。

- 每个状态机有若干个 conf，每个 conf 都代表一个状态。
- 每个状态有**条件**和**动作**两部分，我们应用变量（criterion）时，会按顺序遍历所有**状态**，我们会进入首先满足**条件**的状态，如果进去了一个新的状态，就会执行其**动作**。
    ```shell
    domain: string
        conf: string
            <RULES>
            <ACTS>
    ```

- **RULES** 部分是对 criterion 取值的约束，可以通过**且**和**或**递归地组合起来；不同种类的 `criterion` 约束使用不同的谓词。
- **ACTS** 是一次或多次插件回调的执行。在 ACTS 中，若想参考 criterion 变量当前的值，而非一个定值，这种情况我们支持下面的语法：
    ```shell
    param%criterion%param
    ```

#### **Settings 文件编写示例**

以 `Audio sco` 节点控制为例，当 `sco` 可用，用户也需要时，会通过 `FFmpegCommand` 插件更新采样率，并打开 `sco` 输入输出节点：
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

## **检查工具**

在 `pfw/test` 目录下，我们配置了在 `host` 上使用的 `Makefile`，可以编译生成 `test` 命令行工具，可以在其中输入命令调试同一文件夹下的两个 `.conf` 配置文件，同时在开发 `pfw` 新功能时，也可以借此工具快速检查代码。
```shell
pfw/test$ make cc -o test ../system.c ../parser.c ../context.c ../vector.c ../sanitizer.c ../criterion.c test.c -Wall -Werror -O0 -g -I ../include -D CONFIG_LIB_PFW_DEBUG -fsanitize=address -fsanitize=leak
pfw/test$ ./test
pfw> dump         //打印dump信息
pfw> setint persist.media.MediaVolume 7     //设置MediaVolume的值为7