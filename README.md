# Socketmux 1 2019-1-21

## NAME
Socketmux

## SYNOPSIS
socketmux [port]

## DESCRIPTION
This project is a simple proof of concept for plugable
services in a single binary.

### SERVICES

#### CGI
Socketmux starts a CGI handler on port, defaulted to 8080.

#### Admin Console
Socketmux starts a telnet console on port 8080.

