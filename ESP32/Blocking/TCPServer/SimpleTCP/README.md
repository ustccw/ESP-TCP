### this sample would make your ESP32 interact with any other platform using JSON exchange format 

## Part 1 : main features
1. support multiple tcp client connections, user could define connections by MAX_LIENT_NUMBER
2. support TCP stream group packet, user could define it by TCP_STREAM_HEAD_CHECK
3. support JSON parsing sample and respond to client samplle
4. independent task to manager connections and data processing

## Part 2 : main workflow
1. ESP32 would play the role of TCP Server
2. TCP Client[we choose ubuntu platform] would connect and send JSON formatted data to ESP32
3. ESP32 would parse JSON data and respond to TCP Client
 
## Part 3 : how to make it run
Step 1: config ESP32
- config WiFi SSID and WiFi Password
- config default serial port
- config baud rate[optional]

Step 2: build sample and run TCP Server
```
$ make flash monitor
``` 

Step 3: compile the TCP Client
```
$gcc client.c
```
Step 4: send JSON data by TCP Client
```
./a.out "192.168.111.108" "{\"sample_para\":{\"value\":\"1\"}}"
```

**notes that IP address is the same with ESP32 got, we could see it when tcp server started**

## Part 4 : Notes about the sample
1. you should run hello-world example OK before you start this sample 
2. there care about 4 Bytes JSON-length information in front of TCP data stream 
3. respond to TCP Client not using JSON formatted data based on simplify TCP Client parse process consideration