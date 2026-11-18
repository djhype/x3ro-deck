# Fixture for MakefileDetector
.PHONY: build test
VERSION := 1.0
CC ?= cc
export PATH := $(PATH):bin

build:
	go build ./...

test: build
	go test ./...

%.o: %.c
	$(CC) -c $<

lint fmt: ## two targets
	echo $@

$(BIN): build

clean::
	rm -rf out

long: a \
  b: c

test:
	echo again
