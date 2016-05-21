# ObjectFileSys
Object-based storage(OBS) manages data as objects, as opposed to other storage architectures like file systems which manage data as a file hierarchy. OBS has a lot of benefits such as performance, but in order to support a lot of legacy applications, a file system interface is a must. I have done a few such work. According to my experience, it is feasible to implement such a driver for typical OS systems, such as EMC Atmos, OpenStack Swift, Scality RING, Caringo Swarm, OpenIO and DDN WOS. 
Besides the operating system file system interfaces, it is convenient and must to have a Hadoop File System compatible interface for Apache Spark/Hadoop to analyze unstructured data in OBS.

## Metadata Server
It is critical to design metadata server. There are a few options to choose, such as RDBMS (such as MySQL), NonSQL DB (such as Cassandra), even an existing file system.
Following the choice of platform, the most important non-function feature is performance, due to the possible high frequency file operations.

## Linux platform
It is convenient to use FUSE (https://github.com/libfuse/libfuse) to develop Linux specific file system.

## Windows platform
It is convenient to use DOKAN (https://github.com/dokan-dev/dokany) or CBFS(https://www.eldos.com/cbfs/) to develop Windows specific file system.

## Apache Spark/ Hadoop platform
It is convenient to use JAVA to develop Apache Spark/ Hadoop compatible file system. And it is worth to point that the source code of Hadoop provides a few good examples on Hadoop compatible file system.

## Language Choice
For Linux/ Windows platform file system interfaces are part of the operating system, it is natual to use C++ to develop for the performance reason. Also, the available file system SDKs mainly focuses on C++ examples.
