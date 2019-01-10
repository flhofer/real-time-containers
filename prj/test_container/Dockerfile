FROM multiarch/ubuntu-core:x86_64-bionic
MAINTAINER Florian Hofer (florian.hofer@stud-inf.unibz.it)

LABEL	it.florianhofer.version="0.0.9-beta" \
	vendor1="Florian Hofer" \
	vendor2=Florian\ Hofer \
	it.florianhofer.release-date="2015-02-12" \
	it.florianhofer.version.is-production=""

RUN apt-get update && apt-get install -y \
	libjson-c3 \
	&& rm -rf /var/lib/apt/lists/*

#Notes! here we shoud add users and groups - or run with usermask

RUN mkdir /home/rtuser
WORKDIR /home/rtuser 
COPY rt-app-simple.json .
COPY rt-app .
RUN chmod +x rt-app
ENV PATH="/home/rtuser:${PATH}"

ENTRYPOINT ["rt-app"]
CMD ["rt-app-simple.json"]
