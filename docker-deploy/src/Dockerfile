FROM gcc:9

RUN mkdir /var/log/erss
ADD . /var/log/erss
WORKDIR /var/log/erss
RUN chmod +x startproxy.sh

ENTRYPOINT [ "./startproxy.sh" ]
# FROM ubuntu:20.04 
# RUN apt-get update
# RUN mkdir /var/log/erss
# RUN touch /var/log/erss/proxy.log 
# RUN mkdir /code 
# WORKDIR /code 
# ADD . /code/
# RUN chmod +x startproxy.sh

# ENTRYPOINT [ "./startproxy.sh" ]