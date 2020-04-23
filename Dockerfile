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

RUN sed -i "s/user =.*/user = root/g" /usr/local/wired/etc/wired.conf
RUN sed -i "s/files =.*/files = \/files/g" /usr/local/wired/etc/wired.conf

EXPOSE 4871

CMD ["/usr/local/wired/wired", "-D"]
