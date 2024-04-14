# c-web-server
Web server written in C for studying purposes.

Inspired by [this tutorial](https://dev.to/jeffreythecoder/how-i-built-a-simple-http-server-from-scratch-using-c-739).

## How to run

First, compile the server:
```bash
gcc server.c -o server
```

Then, run it:
```bash
./server
```

The server will be running on `localhost:8080`.

## How to test
Open your browser and go to `localhost:8080/santa-cruz.png`. 
You should then see an image of Santa Cruz, the club I support. :D

There are other files you can test, such as `index.html`.

