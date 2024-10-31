# What is PFW?
*PFW* is the abbriviation of parameter-framework, it is to build state machine based on configuration files and callback functions. The configuration files define the *states*, the *conditions* that define states, and the *criteria* that define conditions. The callbacks define the *actions* of states.

This framework makes the core of **Media Policy of Xiaomi Vela OS**, Though various IOT devices has similar media criteria such as *Audio Mode* and *Available Devices*, the details of stragtegy like *bluetooth device routing* and *volume curve* can be totally different. Therefore, it would be much efficient if we use different configuration files and a configurable codebase.

The idea is from [intel's parameter-framework](https://github.com/intel/parameter-framework), which is too heavy for embedded devices. This frameworks doesn't support parameters except string, dynamic library plugins and XML files, since they cost lots of space but not necessary.

# How to use PFW?
> Let's assume you already have your configuration files and callbacks.

Usually, pfw involves two kinds of users: server and clients. The server should create a pfw object with its configuration files and callback plugins, and maintain it till destroying. In the mean time the clients can modify or query some criteria.

Additionally, if you make some changes to criteria, the actions won't be taken untill calling `pfw_apply`.
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

# How to configure PFW?
This part explains how to write and test your own configuration files and callbacks.

## 1. Write criteria.conf
Each line of this file defines a criterion:
```

<CRITERION> := NumericalCriterion <NAMES> : <RANGES>
            |= ExclusiveCriterion <NAMES> : <VALUES>
            |= InclusiveCriterion <NAMES> : <VALUES>
            |= NumericalCriterion <NAMES> : <RANGES> = <INIT>
            |= ExclusiveCriterion <NAMES> : <VALUES> = <INIT>
            |= InclusiveCriterion <NAMES> : <VALUES> = <INIT>
```

### 1.1 Types
There are 3 kinds of criterion, all based on `int32_t`:
- `NumericalCriterion`: it has value range between `INT32_MIN` and `INT32_MAX`.
- `ExclusiveCriterion`: similar to enum, each numerical value has lteral meaning.
- `InclusiveCriterion`: similar to bitmask, each bit has literal meaning.

Here are some examples:
```
NumericalCriterion MusicVolume : [0,10]
ExclusiveCriterion AudioMode : normal ringtone incall
InclusiveCriterion AvailableDevices : a2dp btsco
```

The `AudioMode` has value range of `{ 0:normal, 1:ringtone, 2:incall }`, the `AvailableDevices` has value range of `{ 0:<none> 1:a2dp, 2:btsco, 3:a2dp|btsco }`.

### 1.2 Names
Each criteriona could have more than one name, for example:
```
NumericalCriterion MusicVolume MediaVolume SportVolume : [0,10]
```

### 1.3 Ranges
For `NumericalCriterion`, you can define more than one interval.
```
<RANGES> := <RANGE> <RANGES>
         |= <RANGE>

<RANGE> := [%d,%d]
        |= [,%d]
        |= [%d,]
        |= %d
```
### 1.4 Initial values
initial values are default zero, you can explicitly define it in need.
```
NumericalCriterion MusicVolume : [0,10] = 5
ExclusiveCriterion AudioMode : normal ringtone incall = normal
```

## 2. Write settings.conf
This file has indentations like python (tab or 4 spaces). Each `domain` of this file defines a state machine. Each `conf` defines a state, if the `RULES` evaluates to be true, the `ACTION` would be taken.
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
It has multi-branch tree structure, the intermediate node is `ALL` or `ANY`, which means `and` and `or`; the leaf node is a judgment on a criterion's value:
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

To combine them together:
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
In the action part, you can write one or multiple callback, they would be called in turns.

## 3. Define callback plugin
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

## 4. Test your configurations on host
There is a test tool for unix host, read the codes for more details.
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