FROM ubuntu:latest

RUN apt-get update && \
	apt-get install -y libjson-c3 libnuma1 libcap2 git gdb check libsubunit0 libgsl23 \
	&& rm -rf /var/lib/apt/lists/*

CMD ["/bin/bash"]

