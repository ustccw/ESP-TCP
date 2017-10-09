### this demo would guide you how to combine a standard JSON package under ESP-IDF  
------------------------------------------------------------------------

### Part 1: How to make it run
##### Step 1: export your IDF path and compiler path
such as the following:
```
$ export IDF_PATH=~/esp/esp-idf
$ export PATH=/opt/xtensa-esp32-elf/bin/:$PATH
```
##### Step 2: make menuconfig
```
$ make menuconfig
```
config:
1. Default serial port
2. Default baud rate // 921600 recommended

##### Step 3: make and run
```
$ make flash monitor
```

### Part 2: Result Show

```
{
    "sensors":  [{
            "id":   "1",
            "temperature1": "23",
            "temperature2": "23",
            "humidity": "55",
            "occupancy":    "1",
            "illumination": "23"
        }, {
            "id":   "2",
            "temperature1": "23",
            "temperature2": "23",
            "humidity": "55",
            "occupancy":    "1",
            "illumination": "23",
            "value":    10
        }]
}

```

### Part 3: Notes about JSON  

1. we adopt standard CJSON lib, header file is under the esp-idf/components/json/include/cJSON.h
2. care about how to free memory

------------------------------------------------------------------------  


