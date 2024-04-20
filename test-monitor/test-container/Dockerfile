FROM ubuntu:jammy
MAINTAINER Florian Hofer (info@florianhofer.it)

LABEL	it.florianhofer.version="0.3.0" \
	vendor1="Florian Hofer" \
	vendor2=Florian\ Hofer \
	it.florianhofer.release-date="2024-04-20" \
	it.florianhofer.version.is-production="no"

RUN apt-get update && apt-get install -y \
	libjson-c5 libnuma1\
	&& rm -rf /var/lib/apt/lists/*

#Notes! here we shoud add users and groups - or run with usermask

RUN mkdir /home/rtuser
RUN mkdir /home/rtuser/log
WORKDIR /home/rtuser 
COPY rt-app-[0-9]*-*.json ./
COPY rt-app-tst*.json ./
COPY rt-app .
RUN chmod +x rt-app
ENV PATH="/home/rtuser:${PATH}"

ENTRYPOINT ["rt-app"]
CMD ["rt-app.json"]
