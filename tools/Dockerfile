FROM ubuntu:latest

RUN apt-get update && \
	apt-get install -y libjson-c5 libnuma1 libcap2 git gdb check libsubunit0 libgslcblas0 \
	&& rm -rf /var/lib/apt/lists/*

CMD ["/bin/bash"]

