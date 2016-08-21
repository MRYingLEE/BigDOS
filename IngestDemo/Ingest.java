
import com.ddn.wos.api.*;

import java.io.*;
import java.util.*;


package com.datastax.driver.examples.datatypes;

import com.datastax.driver.core.*;
import com.datastax.driver.core.utils.Bytes;
import com.google.common.collect.ImmutableMap;

import java.io.*;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.util.Map;

/*
 * Program to ingest a directory and all subdirectories into WOS (file) and Cassandra (metadata).
 *
 * USAGE
 *     java Ingest  <root of path to ingest> <WOS cluster IP addr> <policy> <Cassandra cluster IP addr>
 * EXAMPLE
 *
 *  %  java -classpath wosjava.jar:. Ingest /home/me/myfiles 10.11.0.1 replicate 10.11.0.2
 *
 *
 */

public class Ingest {

    private static int numthreads = 100;
    private static int OBJ_BUFFER_SIZE = 2000000;
    private static String oid_with_zero_length = "0000000000000000000000000000000000000000";

    static private class Create {
        private List filelist;
        private ByteArrayOutputStream manifest;
        private String userPath;
        private Iterator fileitr;
        private String policy;
        private WosCluster wos;
        private CassandraIP;


        // To get the next file to process
        private String getNextFile() {
            synchronized (fileitr) {
                if (fileitr.hasNext()) {
                    return (String) fileitr.next();
                } else {
                    return null;
                }
            }
        }

        //The thread to process one file
        private class CreateWorker extends Thread {

            private int fileProcessedCount = 0;

            public CreateWorker() {
                super("CreateWorker");
            }

            public int getFileProcessedCount() {
                return fileProcessedCount;
            }

            public void run() {
                String fname;
                byte[] data = new byte[OBJ_BUFFER_SIZE];
                while ((fname = getNextFile()) != null) {
                    try {
                        String filepath = userPath + "/" + fname;
                        File file = new File(filepath);
                        long length = file.length();

                        String parent_path=file.getParent();
                        String filename=file.getName();

                        if (length == 0) {
                            // If this file is 0 bytes, skip it.  WOS does not support
                            // ingesting/restoring 0 byte files in v1.1.
                            // But, the meta data should be saved or the file will disappear in file system
                            writeMetaData(parent_path,filename, oid_with_zero_length,0);
                            continue;
                        }

                        WosPutStream puts = wos.createPutStream(policy);
                        puts.setMeta("path", filepath.getBytes());
                        InputStream istream = new BufferedInputStream(new FileInputStream(filepath));
                        long remaining = length;
                        long offset = 0;
                        while (remaining > 0) {
                            int bytesRead = istream.read(data);
                            puts.putSpan(data, offset, bytesRead);
                            remaining -= bytesRead;
                            offset += bytesRead;
                        }
                        istream.close();
                        String oid = puts.close();


                        writeMetaData(fname, oid, length);
                        fileProcessedCount++;
                    } catch (WosException e) {
                        System.out.println("Caught WosException: " + e.getMessage());
                    } catch (IOException e) {
                        System.out.println("Caught IOException: " + e.getMessage());
                    }
                }
            }

        }

        public Create(String userPath, String wosaddr, String policy, String cassandraaddr) {
          //Step 0: To prepare file list

            if (userPath.endsWith("/")) {
                userPath = userPath.substring(0, userPath.length() - 1);
            }

            this.userPath = userPath;
            this.policy = policy;

            this.CassandraIP=cassandraaddr;

            filelist = new LinkedList();

            System.out.println("Discovering files in " + userPath);
            File dirfile = new File(userPath);
            if (!dirfile.isDirectory()) {
                System.out.println("Error: " + userPath + " is not a valid directory");
                System.exit(1);
            }

            getAllFiles(new File(userPath));

            System.out.println(filelist.size() + " files discovered.");

            //Step 1: To process each file in file list
            try {
                //Step 1.1 To prepare WOS connection

                manifest = new ByteArrayOutputStream();
                wos = new WosCluster();
                System.out.println("Connecting to WOS through " + wosaddr + " ...");
                wos.connect(wosaddr);
                System.out.println("Connected.");


                //Step 1.2 To launch threads
                fileitr = filelist.iterator();
                List ingestWorkers = new ArrayList();
                for (int i = 0; i < numthreads; i++) {
                    CreateWorker iw = new CreateWorker();
                    ingestWorkers.add(iw);
                    iw.start();
                }

                //Step 1.3 To join each threads
                int fileProcessedCount = 0;
                try {
                    for (int i = 0; i < numthreads; i++) {
                        CreateWorker iw = (CreateWorker) ingestWorkers.get(i);
                        iw.join();
                        fileProcessedCount += iw.getFileProcessedCount();
                    }
                } catch (InterruptedException e) {
                    ;
                }

                System.out.println(filelist.size() + " files processed.\n");

            } catch (WosException e) {
                System.out.println("Caught WosException: " + e.getMessage());
            } catch (IOException e) {
                System.out.println("Caught IOException: " + e.getMessage());
            }
        }

        // To write meta data information
        private void writeMetaData(String parent_path, String filename, String oid, long filelength) {
            synchronized (this) {
              cassandraSession.execute("INSERT INTO objectfs.objects(parent_path,filename, object_id, filesize) VALUES(?,?,?,?)",
                      parent_path,filename, oid, filelength);
            }
        }

        //To add all files into filelist recursively
        private void getAllFiles(File dir) {
            try {
                if (!dir.canRead()) {
                    System.err.println("File " + dir.getCanonicalPath() + " not readable.  Skipping.");
                    return;
                }

                if (dir.isDirectory()) {
                    String[] children = dir.list();
                    for (int i = 0; i < children.length; i++) {
                        getAllFiles(new File(dir, children[i]));
                    }
                } else {
                    filelist.add(dir.getPath().substring(userPath.length() + 1));
                }
            } catch (IOException e) {
                System.out.println("Caught unexpected IOException: " + e.getMessage());
            }
        }
    }

    //To print the usage information of this program
    private static void usage() {
        System.out.println("java Ingest  <root of path to ingest> <WOS cluster IP addr> <policy> <Cassandra cluster IP addr>");
    }

    private static Cluster cassandraCluster = null;
    private static Session cassandraSession;

    // The main program
    public static void main(String[] args) {
        if (args.length < 4) {
            usage();
            System.exit(1);
        }

        static int PORT = 9042;

        String userPath = args[0];
        String wosaddr = args[1];
        String policy = args[2];
        String cassandraaddr=args[3];

        try {
            cassandraCluster = Cluster.builder()
                    .addContactPoints(cassandraaddr).withPort(PORT)
                    .build();
            cassandraSession = cassandraCluster.connect();

        } finally {
            if (cassandraCluster != null) cassandraCluster.close();
        }

        new Create(userPath, wosaddr, policy,cassandraaddr);
    }
}
