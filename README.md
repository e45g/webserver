# Simple HTTP Server

This project implements a simple HTTP server in C.

## TODO

  - Make CXC less wonky.

## File Structure

### src

  - `server.c`: Main server implementation, including request handling and error management.
  - `server.h`: Header file defining server structures and function prototypes.
  - `routes.c`: Route management, including adding and matching routes.
  - `routes.h`: Header file for route management functions.
  - `utils.c`: Utility functions for logging and environment variable management.
  - `utils.h`: Header file for utility functions.
  - `lib/cJSON`: [cJSON](https://github.com/DaveGamble/cJSON) library.

## Compilation

To compile the server, run:

```bash
make
```
This will create an executable named `server`.

## Running the Server

After compiling, you can run the server with:

```bash
./server
```


## Environment Variables

You can configure the server using a `.env` file. The following variables are supported:

- `PORT`: The port on which the server will listen (default: `1444`).
- `ROUTES_DIR`: The directory where route files are located (default: `./routes`).
- `PUBLIC_DIR`: The directory from which static files will be served (default: `./public`).

### Example `.env` file

```
PORT=8080
ROUTES_DIR=./routes
PUBLIC_DIR=./public
```

## Routes

The server supports custom routes defined in `routes.c`. You can add routes using the `add_route` function, specifying the HTTP method, path, and callback function.

### Example Routes

- `GET /`: Serves the `example/index.html` file.
- `POST /post_test`: Responds with a JSON object.
