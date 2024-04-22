FROM ubuntu:jammy

MAINTAINER Florian Hofer (info@florianhofer.it)

RUN apt-get update && \
		apt-get install -y wget sudo unzip libusb-1.0-0-dev procps
	
ENV CDS_VERSION "4.11.0.0"
ENV EDGE_VERSION "4.11.0.0"

ENV URL "https://store-archive.codesys.com/ftp_download/3S/LinuxSL/2302000005/$CDS_VERSION/CODESYS%20Control%20for%20Linux%20SL%20$CDS_VERSION.package"

ENV EDGE_URL "https://store-archive.codesys.com/ftp_download/3S/EdgeGatewayLinux/000120/$EDGE_VERSION/CODESYS%20Edge%20Gateway%20for%20Linux%20$EDGE_VERSION.package"

RUN wget --output-document=/tmp/codesys.package $URL && \
		unzip -p /tmp/codesys.package '*codemeter*.deb' > /tmp/codemeter.deb && dpkg -i /tmp/codemeter.deb && \
		unzip -p /tmp/codesys.package '*codesyscontrol*.deb' > /tmp/codesys.deb && dpkg -i /tmp/codesys.deb
		
RUN wget --output-document=/tmp/edge.package $EDGE_URL && \
		unzip -p /tmp/edge.package '*amd64.deb' > /tmp/edge.deb && dpkg -i /tmp/edge.deb
	
EXPOSE 11740 1217 11743

ENTRYPOINT ["/bin/sh", "-c" , "/etc/init.d/codesyscontrol start"]
