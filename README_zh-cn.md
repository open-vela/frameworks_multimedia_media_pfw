# PFW 是什么？
这个框架基于配置文件和回调函数构建状态机。配置文件定义了“状态”，定义状态的“条件”，以及定义条件的“criteria”。回调函数定义了状态的“操作”。

这个框架构成了**Xiaomi Vela OS Media Policy**的核心内容。虽然各种物联网设备都具有类似的媒体标准，比如*音频模式*和*可用设备*，但是诸如*蓝牙设备路由*和*音量曲线*之类的策略细节可能会完全不同。因此，如果我们使用不同的配置文件和可配置的代码仓库，将会更加高效。

这个想法源自[intel's parameterframework](https://github.com/intel/parameter-framework), 但是这个框架对嵌入式设备来说过code size开销太大。考虑到string以外的参数，动态库插件，和XML并非必要，此框架并不支持。

# 如何使用 PFW？
> 让我们先假设已经配置好了 PFW

通常情况下，pfw 涉及两种用户：server 和 client 。server 应该使用其配置文件和回调插件创建一个pfw对象，并维持该对象直到销毁。同时， client 可以修改或查询某些 criteria。

另外，如果对条件进行了一些更改，则在调用“pfw_apply”之前不会执行任何操作。
```C
// modify
int pfw_setint(void* handle, const char* name, int value);
int pfw_setstring(void* handle, const char* name, const char* value);
int pfw_include(void* handle, const char* name, const char* value);
int pfw_exclude(void* handle, const char* name, const char* value);
int pfw_increase(void* handle, const char* name);
int pfw_decrease(void* handle, const char* name);
int pfw_reset(void* handle, const char* name);

// apply
void pfw_apply(void* handle);

// query
int pfw_getint(void* handle, const char* name, int* value);
int pfw_getstring(void* handle, const char* name, char* value, int len);
int pfw_getrange(void* handle, const char* name, int* min_value, int* max_value);
int pfw_contain(void* handle, const char* name, const char* value,
    int* contain);
```

# 如何配置 PFW?
这部分会解释如何写自己的配置文件和 callback 插件。

## 1. 编写 criteria.conf
每行都定义了一个criterion
```

<CRITERION> := NumericalCriterion <NAMES> : <RANGES>
            |= ExclusiveCriterion <NAMES> : <VALUES>
            |= InclusiveCriterion <NAMES> : <VALUES>
            |= NumericalCriterion <NAMES> : <RANGES> = <INIT>
            |= ExclusiveCriterion <NAMES> : <VALUES> = <INIT>
            |= InclusiveCriterion <NAMES> : <VALUES> = <INIT>
```

### 1.1 Types
变量的本质上都是int32_t，但是取值会可以被解释为三种类型：
- `NumericalCriterion`: 数值型，即最原始的int型，取值范围是INT32_MIN到INT32_MAX。
- `ExclusiveCriterion`: 枚举型，类似C语言的enum，从0开始的若干个值被解释为有意义的字符串。
- `InclusiveCriterion`: 掩码型，从低位到高位的若干个bit被解释为有意义的字符串。

下面给出一些例子：
```
NumericalCriterion MusicVolume : [0,10]
ExclusiveCriterion AudioMode : normal ringtone incall
InclusiveCriterion AvailableDevices : a2dp btsco
```

`AudioMode`的取值范围是`{ 0:normal, 1:ringtone, 2:incall }`，`AvailableDevices`的取值范围是`{ 0:<none> 1:a2dp, 2:btsco, 3:a2dp|btsco }`.

### 1.2 Names
每个变量可以有1个或更多的名字:
```
NumericalCriterion MusicVolume MediaVolume SportVolume : [0,10]
```

### 1.3 Ranges
对于`NumericalCriterion`，可以添加多个区间作为取值范围。
```
<RANGES> := <RANGE> <RANGES>
         |= <RANGE>

<RANGE> := [%d,%d]
        |= [,%d]
        |= [%d,]
        |= %d
```
### 1.4 Initial values
可以给每个变量定一个初始值，如果不指定则默认为0。
```
NumericalCriterion MusicVolume : [0,10] = 5
ExclusiveCriterion AudioMode : normal ringtone incall = normal
```

## 2. 编写 settings.conf
这个文件像python一样有用缩进来区分层次 (tab or 4 spaces)。每个`domain`定义了以个状态机，每个`conf`定义了一个状态，如果`RULES`部分的取值为真，则`ACTION`中指定的回调函数会被执行.
```
domain: xxx
    conf: xxx
        <RULES>
        <ACTION>
    conf: xxx
        <RULES>
        <ACTION>
```

### 2.1 Rules
拥有多叉树结构, 中间节点为`ALL`或`ANY`，分别表示`且`和`或`，叶节点是对一个criterion取值的判断。
- `NumericalCriterion`:
    ```
    MusicVolume In [,5]
    MusicVolume NoIn [6,]
    ```
- `ExclusiveCriterion`:
    ```
    AudioMode Is incall
    AudioMode IsNot incall
    ```
- `IncludesiveCriterion`:
    ```
    AvailableDevices Includes a2dp
    AvailableDevices Excludes a2dp
    ```

把它们合并到一起，会有下面的结构:
```
ANY
    ALL
        AudioMode Is ringtone
        AvailableDevices Includes btsco
        RingVolume In [6,]
    ALL
        AudioMode Is normal
        MusicVolume In [6,]
```

### 2.2 Action
这个部分你可以写多种多次callback，它们都会依次执行

## 3. 定义 callback 插件
```
typedef void (*pfw_callback_t)
    (void* cookie, const char* params);

typedef struct pfw_plugin_def_t {
    const char* name;
    void* cookie;
    pfw_callback_t cb;
} pfw_plugin_def_t;

void* pfw_create(..., pfw_plugin_def_t* defs, int nb， ...);
```

## 4. 在 host 环境测试你的配置
通过下面的命令可以使用一个 unix host 上可用的测试工具，阅读代码可以看到更多功能细节。
```
cd pfw/test
make
./test
```

```
$ ./test
[pfw_test_callback] id:0 params:pcm0p,set_parameter,set_scenario=music;MixSpeaker,weights,1 1 1 1 1 0;
[pfw_test_callback] id:0 params:SelSCO,map,-1;
[pfw_test_callback] id:0 params:SelPlay,map,0 -1;
[pfw_test_callback] id:0 params:SelAlarm,map,0 -1;
[pfw_test_callback] id:0 params:SCOtx,pause,;
[pfw_test_callback] id:0 params:pcm0c,play,;SelCap,map,0 0 -1;
[pfw_test_callback] id:0 params:;
[pfw_test_callback] id:0 params:VolSCO,volume,-20dB;
[pfw_test_callback] id:0 params:VolRing,volume,-20dB;
[pfw_test_callback] id:0 params:VolMedia,volume,-20dB;
[pfw_test_callback] id:0 params:VolAlarm,volume,-20dB;
pfw> dump
+-------------------------------------------------------------
| CRITERIA                         | STATE    | VALUE
+-------------------------------------------------------------
| AudioMode                        | 0        | normal
| UsingDevices                     | 1        | mic
| AvailableDevices                 | 0        | <none>
| HFPSampleRate                    | 0        | 8000
| ActiveStreams                    | 0        | <none>
| Ring                             | 0        | amovie_async@Ring
| Music                            | 0        | amovie_async@Music
| Sport                            | 0        | amovie_async@Media
| Alarm                            | 0        | amovie_async@Alarm
| Enforced                         | 0        | amovie_async@Enforced
| Video                            | 0        | movie_async@Video
| VideoSink                        | 0        | moviesink_async
| AudioSink                        | 0        | amoviesink_async
| PictureSink                      | 0        | vmoviesink_async
| persist.media.MuteMode           | 0        | off
| persist.media.SCOVolume          | 5        | 5
| persist.media.RingVolume         | 5        | 5
| persist.media.MediaVolume        | 5        | 5
| persist.media.AlarmVolume        | 5        | 5
+-------------------------------------------------------------
| DOMAIN                           | CONFIG
+-------------------------------------------------------------
| SpeakerDomain                    | music
| SCOrxDomain                      | default
| PlayDomain                       | speaker
| AlarmDomain                      | speaker
| SCOtxDomain                      | default
| MicDomain                        | capture

| IncallVolumeDomain               | X
| TTSVolumeDomain                  | 5
| RingVolumeDomain                 | 5
| MediaVolumeDomain                | 5
| AlarmVolumeDomain                | 5
+-------------------------------------------------------------
ret 0
pfw> setint persist.media.MediaVolume 7
ret 0
pfw> apply
[pfw_test_callback] id:0 params:VolSCO,volume,-12dB;
[pfw_test_callback] id:0 params:VolMedia,volume,-12dB;
ret 0
pfw>
```