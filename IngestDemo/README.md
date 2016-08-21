#Ingest
Ingest is the process to import traditional files to object storage (OS).

## Reason
Although it is convenient to use FILE API to ingest files, a better method is to  use native OS API to do so. 

##The advantage includes:
  to break the limit of OS system for not all OS system have streaming API, which is definitely needed by traditional FILE API;
  
  to maximum the speed, which could be dozens speed of the FILE API;
  
  to implement application logic beyond FILE system.

##Demo
Here is a demo to do so. This demo is based on a demo of DDN.

The source code is at ALPHA stage. You may use it at your own risk. So don't use it in your production environment.
