# Arduino Web Server

Pushing an Arduino Uno to its limits by having it act as a web server capable of handling 8 simultaneous clients and performing (very) basic method and path handling. 

There was no HTTP library or framework used, and the server exposed almost certainly is not the best behaved, but you can point your browser at it and have it display a basic web page. This is, by all accounts, a toy project that was done for fun (and maybe to win a bet) and really shouldn't be used in any serious context.

Assuming the Arduino hasn't given up, it can be accessed here: https://arduino.b3dy.io/

## Hardware Needed

All that is really needed is: 
- Arduino Uno
- Ethernet shield 

Plug the shield into your Arduino and you're setup

## Running on the Arduino

1. Plug the Arduino Uno into your computer
2. Open this project in the Arduino IDE
3. Flash the Arduino
4. Point your computer's web browser to your Arduino's local IP
5. Realize your new found power 

## Benchmarks 

Lets see this lil Arduino flex: 

A basic benchmark was run with [oha](https://github.com/hatoo/oha): 

```bash
$ oha -z 60s http://<arduinos-ip>/
```

and the results: 

\*\*drum roll\*\*

```
Summary:
  Success rate:	1.0000
  Total:	60.0038 secs
  Slowest:	35.0564 secs
  Fastest:	0.0198 secs
  Average:	0.2636 secs
  Requests/sec:	48.6636

  Total data:	1.27 MiB
  Size/request:	455 B
  Size/sec:	21.62 KiB

Response time histogram:
  0.020 [1]     |
  3.523 [2910]  |■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
  7.027 [5]     |
  10.531 [1]    |
  14.034 [1]    |
  17.538 [0]    |
  21.042 [1]    |
  24.545 [0]    |
  28.049 [0]    |
  31.553 [0]    |
  35.056 [1]    |

Latency distribution:
  10% in 0.0210 secs
  25% in 0.0217 secs
  50% in 0.0352 secs
  75% in 0.0402 secs
  90% in 1.0375 secs
  95% in 1.0509 secs
  99% in 2.0929 secs

Status code distribution:
  [200] 2920 responses
```