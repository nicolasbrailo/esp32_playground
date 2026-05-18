# blawifi

Creates a fake AP in ESP32 that can be used to provision device config. Once a user provides the config, a callback is invoked and the fake AP is shut down.

The config params can be changed by editting the HTML files and their handler in wifi_provision.h/c

