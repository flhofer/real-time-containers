FROM multiarch/ubuntu-core:x86_64-bionic
MAINTAINER Florian Hofer (florian.hofer@stud-inf.unibz.it)

LABEL	it.florianhofer.version="0.2.0" \
	vendor1="Florian Hofer" \
	vendor2=Florian\ Hofer \
	it.florianhofer.release-date="2019-04-12" \
	it.florianhofer.version.is-production="no"

RUN apt-get update && apt-get install -y \
	libjson-c3 \
	&& rm -rf /var/lib/apt/lists/*

#Notes! here we shoud add users and groups - or run with usermask

RUN mkdir /home/rtuser
RUN mkdir /home/rtuser/log
WORKDIR /home/rtuser 
COPY rt-app-tst*.json ./
COPY rt-app .
RUN chmod +x rt-app
ENV PATH="/home/rtuser:${PATH}"

ENTRYPOINT ["rt-app"]
CMD ["rt-app.json"]
