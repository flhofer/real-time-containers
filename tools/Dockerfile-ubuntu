FROM ubuntu:latest

RUN apt-get update && \
	apt-get install -y libjson-c5 libnuma1 libcap2 git gdbserver valgrind check libsubunit0 libgsl27 \
	&& rm -rf /var/lib/apt/lists/*

CMD ["/bin/sh"]

