#Ingest
Ingest is the process to import traditional files to object storage (OS).

## Reason
Although it is convenient to use FILE API to ingest files, a better method is to  use native OS API to do so. 

##The advantage includes:
  to break the limit of OS system for not all OS system have streaming API, which is definitely needed by traditional FILE API;
  
  to maximum the speed, which could be dozens speed of the FILE API;
  
  to implement application logic beyond FILE system.

##Demo
Here is a demo to do so. This JAVA Program ingests a directory and all subdirectories into WOS (file) and Cassandra (metadata).

The source code is at ALPHA stage. You may use it at your own risk. So don't use it in your production environment.

## Usage
  java Ingest  <root of path to ingest> <WOS cluster IP addr> <policy> <Cassandra cluster IP addr>
 
 EXAMPLE
  %  java -classpath wosjava.jar:. Ingest /home/me/myfiles 10.11.0.1 replicate 10.11.0.2
