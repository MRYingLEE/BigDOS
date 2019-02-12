# BigDOS
Object-based storage(OBS) manages data as objects, as opposed to other storage architectures like file systems which manage data as a file hierarchy. OBS has a lot of benefits such as performance, but in order to support a lot of legacy applications, a file system interface is a must. I have done a few such work. According to my experience, it is feasible to implement such a driver for typical OS systems, such as EMC Atmos, OpenStack Swift, Scality RING, Caringo Swarm, OpenIO and DDN WOS. 
Besides the operating system file system interfaces, it is convenient and must to have a Hadoop File System compatible interface for Apache Spark/Hadoop to analyze unstructured data in OBS.

In short, BigDOS is a Decentralized File System for Unstructured Big Data. The Linux file system driver is developed by c++. The driver uses Cassandra for file profile and uses an Object Storage System for file contents. BigDOS.pdf is the Product Whitepaper. 

## Metadata Server <font color="red">Implemented</font>
It is critical to design metadata server. There are a few options to choose, such as RDBMS (such as MySQL), NonSQL DB (such as Cassandra), even an existing file system.
Following the choice of platform, the most important non-function feature is performance, due to the possible high frequency file operations.

### Implementation：Cassandra script to create BigDOS application schema

```
CREATE KEYSPACE BigDOS_sys WITH replication = {'class': 'SimpleStrategy', 'replication_factor': '1'}  AND durable_writes = true;

CREATE TABLE BigDOS_sys.BigDOS_groups (
    groupid int PRIMARY KEY,
    displayname text,
    members set<int>,
	membernames set<text>
);

CREATE ROLE dba WITH PASSWORD = 'dba' AND LOGIN = true AND SUPERUSER = false;

CREATE TABLE BigDOS_sys.BigDOS_dba (
    loginrole text PRIMARY KEY,
    alwayscache boolean,
    cachefolder text,
    dbname text,
    displayname text,
    linux_belongedgroups set<bigint>,
    linux_primarygroupid bigint,
    linux_primarygroupname text,
    linux_userid bigint,
    mountfolder text,
    ttl_read bigint,
    ttl_write bigint,
    wosdel boolean,
    wosip text,
    wospolicy text,
	license_code text
);

insert into BigDOS_sys.BigDOS_dba(loginrole, displayname,linux_userid,  linux_primarygroupid ,linux_primarygroupname , linux_belongedgroups, wosip, wospolicy, wosdel,  dbname,  mountfolder, cachefolder,alwayscache, ttl_read, ttl_write, license_code)
values ('dba','dba',0, 0, 'Administrators', {0},'192.168.2.16', 'all', false,'BigDOS_app1','/root/BigDOS_app1',  '/root/BigDOS_app1.file',false, 600,600,'Evaluation');

GRANT SELECT ON KEYSPACE BigDOS_sys TO dba;
GRANT MODIFY ON KEYSPACE BigDOS_sys TO dba;

GRANT SELECT ON KEYSPACE BigDOS_app1 TO dba;
GRANT MODIFY ON KEYSPACE BigDOS_app1 TO dba;


CREATE ROLE shenzhen WITH PASSWORD = 'shenzhen' AND LOGIN = true AND SUPERUSER = false;

CREATE TABLE BigDOS_sys.BigDOS_shenzhen (
    loginrole text PRIMARY KEY,
    alwayscache boolean,
    cachefolder text,
    dbname text,
    displayname text,
    linux_belongedgroups set<bigint>,
    linux_primarygroupid bigint,
    linux_primarygroupname text,
    linux_userid bigint,
    mountfolder text,
    ttl_read bigint,
    ttl_write bigint,
    wosdel boolean,
    wosip text,
    wospolicy text,
	license_code text
);

insert into BigDOS_sys.BigDOS_shenzhen(loginrole,displayname, linux_userid,  linux_primarygroupid ,linux_primarygroupname , linux_belongedgroups, wosip, wospolicy, wosdel,  dbname,  mountfolder, cachefolder,alwayscache, ttl_read, ttl_write, license_code)
values ('shenzhen','demo user',10001, 10001, 'Common users', {10001},'192.168.2.16', 'all', false,'BigDOS_app1','/root/BigDOS_app1',  '/root/BigDOS_app1.file',false, 600,600,'Evaluation');

GRANT SELECT ON BigDOS_sys.BigDOS_shenzhen TO shenzhen;

GRANT SELECT ON KEYSPACE BigDOS_app1 TO shenzhen;
GRANT MODIFY ON KEYSPACE BigDOS_app1 TO shenzhen;

insert into BigDOS_sys.BigDOS_groups (groupid, displayname,members, membernames )
values (0,'Managers',{0},{'Manager'});
insert into BigDOS_sys.BigDOS_groups (groupid, displayname,members, membernames )
values (10001,'Common users',{10001},{'shenzhen'});
```

## Linux platform <Implemented>
It is convenient to use FUSE (https://github.com/libfuse/libfuse) to develop Linux specific file system.

### The Used Libs 
```

#define BigDOS_VERSION 			"1.5.1"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 29
#endif

#include <fuse.h> //LY: Critical Lib, the version must be checked carefully

#include <string.h>
#include <unistd.h>

#include <stdio.h>      /* printf, scanf, NULL */
#include <stdlib.h>     /* malloc, free, rand */

#include <dirent.h>
#include <errno.h>
#include <sys/time.h>

#ifndef HAVE_SETXATTR
#define  HAVE_SETXATTR
#endif


#include <cstdio>
#include <cstdlib>
#include <stddef.h>
#include <pthread.h>
#include <vector>
#include <wos_cluster.hpp>  //LY: WOS specific
#include <wos_obj.hpp>  //LY: WOS specific

#include <syslog.h>
#include <limits.h>
#include <exception>
#include <map>
#include <utility>
#include <string>
#include <iostream>
#include <fstream>      // std::fstream
#include "common.h"
#include "cqlfs/cqlfs.h"
//#include "linkfs/LinkFS.h"
#include "localfs/LocalFS.h"
#include "oidfs/OidFS.h"


#include <boost/filesystem.hpp>

#include <boost/format.hpp>
//#include <boost/convert.hpp>
#include <unistd.h>
/* strrchr example */

#include <boost/system/config.hpp>
#include <boost/cstdint.hpp>
#include <boost/assert.hpp>
#include <boost/noncopyable.hpp>
#include <boost/utility/enable_if.hpp>
#include <ostream>
#include <stdexcept>
#include <boost/cerrno.hpp>

#include <cuckoohash_map.hh>

```

### Todo list
```
 //Coding Improvement--Robust
 //To my surprise, try... catch structure cannot catch all exceptions. 
 //If there is no detail check step by step, it is easy to meet a crash.
// Exceptions aren't necessarily the reason for program crashes- segmentation faults, for example, aren't able to be caught with try / catch.
// C++ try - catch blocks only handle C++ exceptions.Errors like segmentation faults are lower - level,
// and try - catch ignores these events and behaves the same as if there was no try - catch block.
// TODO: to make a Cassandra framework to call statement ,process exception.

 //DONE TODO: to always use try exception structure to implement stability.
 //DONE TODO: to catch wos exception: (std::exception subclass)
 //TODO: when WOS/Cassandra is offline, try to reconnect and don't fail directly. Try to use local cache at first.
 //TODO: Reconnect function and a special exeption of "not connected".
//DONE: TODO: Memory leak checking
//DONE TODO: to use ObjectHandle to avoid complex logical in 
//DONE TODO: to make path2OID as part of cache in cql_with_cache
//DONE: TODO: to make a attributes cache in cql_with_cache when readdir, 
//TODO: to remove cache when an external command or TTL
//DONE TODO: to confirm IO stream ptrs are auto free. And also free the ptrs when release!!!
//TODO: to use multi contacts for Cassandra
//TODO: to use tmp folder instaed of cached file, so that no parameter of file cache!
//TODO: urgent! big change! save folder/file into the same table paths, maybe named as paths. New col: file_status int. Reasons: efficiency and easy coding.
//DONE TODO: to change oid to targetpath 

 //Coding Improvement-- style
 //DONE TODO: To make fi->fh has more information built in. Maybe a structure {int high, int filehandle}
 //DONE (Not necessary) TODO: To make FUSE and DOKANY have consistent interface to support user specific file attached information by alter source code of FUSE!
 //TODO: to consist all return values: Success; Failed; NotMyJob;
 //TODO: to consist file attributes API by using string and boost::filesystem  //This is not a good idea for there are too many low level operations
 //TODO: to check using lstat/stat and other variants.
 //DONE:TODO: to support direct delete if global config allows
 //DONE: TODO: to add lock global config , TTL for readlock, writelock.(0 means no lock instead of lock forever)
//TODO: to use official lock events!

 //Coding Improvement--Performance
 //TODO: to support OIDfs with length and timestamp, User assigned postfix is supported.
 //TODO: thread safe by thread safe data structures (Partly done)
//TODO: to use Cassandra Prepared Statements to improve performance (http://datastax.github.io/cpp-driver/topics/basics/prepared_statements/)
//TODO: to make API based on boost::filesystem::path for all non-OID files, instead of const char*.
//TODO: to make OID API based on WosOID instead of const char*.
//TODO: to use write_buf, read_buf instead of write, read. so that to support splice copy.
//TODO: to change the buf of FUSE in FUSE SOURCE CODE to 1M to be the same as that of WOS.

 //Coding Improvement- Security
 //TODO: to use Cassandra id-password to protect OID information.
 //	HardLink is not supported due to cross file system boundaries, but symbolic (soft) link works!
//TODO: to use cassandra login ID/Password as security checking when app starts

 // nullptr will be returned when fails.
//DONE TODO: urgent: Folders or not, mode or not?
//DONE TODO: to add a name space parameter in main.
//DONE TODO: to chekc oid_with_zero_length always when open.
//TODO: to alter Objects by adding original_length (meaningful for reserved and zerolength file)


// Pure tech
//TODO: to seperate CassandraFS, FileLog, Access Control and other modules into external dynamic library.
//TODO: to log ALL Cassandra and WOS exceptions and error messages.
//TODO: To log Cassandra into system log
//TODO: to define cassandra retry policy ( http://datastax.github.io/cpp-driver/topics/configuration/retry_policies/
//TODO: to use linux error code (http://man7.org/linux/man-pages/man3/errno.3.html)

```

## Windows platform  <NOT Implemented>
It is convenient to use DOKAN (https://github.com/dokan-dev/dokany) or CBFS(https://www.eldos.com/cbfs/) to develop Windows specific file system.

## Apache Spark/ Hadoop platform  <Implemented>
It is convenient to use JAVA to develop Apache Spark/ Hadoop compatible file system. And it is worth to point that the source code of Hadoop provides a few good examples on Hadoop compatible file system.

WosFS-for-Spark is the implementation. WOS-FS is Hadoop File System Compatible. WOS-FS brings WOS of DDN.com to Apache Spark cosystem. The github repositories can be found at https://github.com/MRYingLEE/WosFS-for-Spark.

## Language Choice
For Linux/ Windows platform file system interfaces are part of the operating system, it is natual to use C++ to develop for the performance reason. Also, the available file system SDKs mainly focuses on C++ examples.
