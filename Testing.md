from ./xv6/


```
git pull

source ~/esp/esp-idf/export.sh && make esp32s3

make esp32s3-flash ESP_PORT=/dev/cu.usbmodem1101 ESP_BAUD=460800; tinygo monitor
```

