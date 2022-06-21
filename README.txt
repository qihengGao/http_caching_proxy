please run 'sudo docker-compose up -d' under 'docker-deploy' directory

run 'sudo docker ps' to check the id

run 'sudo docker kill <id>' to kill the daemon

after running the docker, the 'proxy.log' will be generated under ./docker-deploy/src

To see the functionality of our proxy server, here takes Firefox for the example of configuring.
Preference->General->Network Settings
Select 'Manual proxy configuration' and set port number as 12345
