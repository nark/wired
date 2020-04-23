FROM ubuntu:18.04

RUN apt-get update
RUN apt-get install -y build-essential git libsqlite3-dev libxml2-dev libssl-dev zlib1g-dev autoconf

RUN mkdir /wired
ADD . /wired/

WORKDIR /wired
RUN git submodule update --init --remote
RUN bash /wired/libwired/bootstrap

RUN ./configure

RUN make
RUN make install

CMD /usr/local/wired/wiredctl start
EXPOSE 4871