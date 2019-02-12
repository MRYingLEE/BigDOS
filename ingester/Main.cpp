#include <iostream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <chrono>  // for high_resolution_clock
#include <iostream>
#include <string>
#include <memory>
#include <utility>
#include <wos_cluster.hpp>  //LY: WOS specific
#include <wos_obj.hpp>  //LY: WOS specific
#include <fcntl.h>

#include "CTPL-master/ctpl.h"

#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "wof/wof_opt.h"
#include "fuse2.9.7/fuse_opt.h"

//#define debug

#ifdef debug
int file_No = 0;
#endif

using namespace boost::filesystem;
bool multithreaded = true;

//==================== public virables
size_t unit_size = 32 * 1024 * 1024;

ctpl::thread_pool upload_thread_pool(10);
ctpl::thread_pool print_thread_pool(1);
//ctpl::thread_pool file_parts_thread_pool(1);

static wosapi::WosClusterPtr wos_global;
static wosapi::WosPolicy policy;

//=================== file and part description class

class fileInfo; //corrensponding to a file 

class partInfo { // A file can have a few parts
public:
	wosapi::WosOID	oid;
	fileInfo* parentfile; //point to the parent file

	off_t offset;
	size_t size; // Except the last one, all parts have the same unit_size.

	//uint32_t Retrytimes = 0;
	//bool Succeeded;
	std::string *partlabel;
	partInfo() {
	}

	~partInfo() {
		parentfile = nullptr; //in case cross delete
	}
};

using parts = std::vector<partInfo*>;

class fileInfo {
public:
	wosapi::WosOID mOID; //Multi parts OID or a normal OID

	boost::filesystem::path filepath;
	long unsigned int filehandle = 0;
	size_t size = 0;
	uint32_t totalParts = 0;
	uint32_t Succeeded_Parts = 0;
	parts fileparts;
	size_t filenumber;
	//	bool done=false;
	std::mutex filemutex;
};

//===================== Print utilities to show the progress

void PrintMessage(int index, std::string * message)
{
	std::cout << *message << '\n';

	delete message;
}

void PrintErrorMessage(int index, int uploadindex, std::string * message)
{
	std::cout << "Error Message:" << *message << " upload thread= " << uploadindex << '\n';

	delete message;
}

void PrintPartUploaded(int index, int uploadindex, long unsigned int fileHandle, off_t offset, size_t size, std::string* filepath, std::string* oid, size_t filenumber, std::string* partlabel)
{
	std::cout << "Uploaded:" << *partlabel << " of file(" << filenumber << "):" << *filepath << " file handle:" << fileHandle << " offset= " << offset << " size = " << size << " upload thread= " << uploadindex << " oid= " << *oid << '\n';
	delete filepath;
	delete oid;
	delete partlabel;
}

void PrintFileUploaded(int index, int uploadindex, size_t size, std::string* filepath, std::string* oid, size_t fileNumber)
{
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	std::time_t now_c = std::chrono::system_clock::to_time_t(now);

	std::cout << std::put_time(std::localtime(&now_c), "%F %T") << " Uploaded:" << fileNumber << " file:" << *filepath << " size = " << size << " upload thread= " << uploadindex << " oid= " << *oid << '\n';
	delete filepath;
	delete oid;
}

//===================== Multi parts upload utilities
// Given a list of OIDs, stores each OID in a multipart stream to
// assemble a larger object. Returns the multipart OID.
wosapi::WosOID CreateMultipartObject(const wosapi::WosClusterPtr& wos,
	const wosapi::WosPolicy& policy,
	const std::vector<partInfo*> oids)
{
#ifdef debug
	file_No = file_No + 1;
	return wosapi::WosOID(std::to_string(file_No));
#else

	wosapi::WosPutStreamPtr puts = wos->CreateMultiPartPutStream(policy);
	for (std::vector<partInfo*>::const_iterator it = oids.begin();
		it != oids.end(); ++it)
	{
		wosapi::WosStatus status;
		puts->PutPart(status, (*it)->oid);
	}
	wosapi::WosStatus status;
	wosapi::WosOID moid;
	puts->Close(status, moid);
	return moid;
#endif
}

//===================== Upload utilities
// To upload a buffer
wosapi::WosOID UploadBuffer(char* buffer, size_t size, int uploadindex)
{
#ifdef debug
	file_No++;
	return wosapi::WosOID(std::to_string(file_No));
#else
	wosapi::WosObjPtr w = wosapi::WosObj::Create();

	w->SetData(buffer, size);

	wosapi::WosStatus s;
	wosapi::WosOID oid;

	int triedtimes = 0;

	while (triedtimes < 5)
	{

		wos_global->Put(s, oid, policy, w);

		if (s != wosapi::ok)
		{
			if (multithreaded)
				print_thread_pool.push(PrintErrorMessage, uploadindex, new std::string(s.ErrMsg()));
			else
				PrintErrorMessage(0, uploadindex, new std::string(s.ErrMsg()));

			++triedtimes;
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		else
			break;
	}

	return oid;
#endif
}

// After a part is uploaded, create multi parts object if needed
void parts_done(int index, long unsigned int fileHandle, off_t offset, size_t size, partInfo* partinfo)
{
	fileInfo* fileinfo = partinfo->parentfile;

	if (multithreaded)
		std::lock_guard<std::mutex> lock(fileinfo->filemutex);

	++(fileinfo->Succeeded_Parts);

	if (fileinfo->Succeeded_Parts == fileinfo->totalParts) // All parts have been uploaded
	{
		close(fileHandle);

		fileinfo->mOID = CreateMultipartObject(wos_global, policy, fileinfo->fileparts);

		for (auto& part : fileinfo->fileparts)
		{
			delete part;
		}

		fileinfo->fileparts.clear();

		std::string* filepath = new std::string(fileinfo->filepath.string());
		std::string* oid = new std::string(fileinfo->mOID);

		if (multithreaded)
			print_thread_pool.push(PrintFileUploaded, index, size, filepath, oid, fileinfo->filenumber);
		else
			PrintFileUploaded(0, index, size, filepath, oid, fileinfo->filenumber);

		delete fileinfo;
	}
}

void UploadPart(int index, long unsigned int fileHandle, off_t offset, size_t size, partInfo* partinfo)
{
	char* buffer = new char[size];

	int res = pread(fileHandle, buffer, size, offset);

	if (res == -1)
	{
		delete[] buffer;
		return;
	}

	partinfo->oid = UploadBuffer(buffer, size, index);
	delete[] buffer;

	std::string* filepath = new std::string(partinfo->parentfile->filepath.string());
	std::string* oid = new std::string(partinfo->oid);
	std::string* label = new std::string(*partinfo->partlabel);

	if (multithreaded)
		print_thread_pool.push(PrintPartUploaded, index, fileHandle, offset, size, filepath, oid, partinfo->parentfile->filenumber, label);
	else
		PrintPartUploaded(0, index, fileHandle, offset, size, filepath, oid, partinfo->parentfile->filenumber, label);

	parts_done(index, fileHandle, offset, size, partinfo);
	//file_parts_thread_pool.push(parts_done, fileHandle, offset, size, partinfo);
}

void UploadFile(int index, boost::filesystem::path path, size_t size, size_t filenumber)
{
	const char * filepath_c_str = path.string().c_str();

	long unsigned int fileHandle = open(filepath_c_str, O_RDONLY);

	char* buffer;

	int triedtimes = 0;

	while (triedtimes < 5)
	{
		try
		{
			buffer = new char[size];
			if (buffer != nullptr)
				break;
			else
				++triedtimes;
		}
		catch (...)
		{
			++triedtimes;
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}

	int res = pread(fileHandle, buffer, size, 0);

	close(fileHandle);

	if (res == -1)
	{
		delete[] buffer;
		return;
	}

	wosapi::WosOID	mOID = UploadBuffer(buffer, size, index);

	std::string* filepath = new std::string(path.string());
	std::string* oid = new std::string(mOID);

	delete[] buffer;

	if (multithreaded)
		print_thread_pool.push(PrintFileUploaded, index, size, filepath, oid, filenumber);
	else
		PrintFileUploaded(0, index, size, filepath, oid, filenumber);
}

//template<typename F, typename... Rest>
//auto upload_thread_pool_push(F && f, Rest&&... rest) ->std::future<decltype(f(0, rest...))> {
//	if (multithreaded)
//		upload_thread_pool.push(f, ...);
//	else
//		**f(...);
//}

//======================= To process each file
int processFile(boost::filesystem::path path, size_t fileNumber)
{
	const char * path_c_str = path.string().c_str();

	struct stat stbuf;
	int	res = lstat(path_c_str, &stbuf);

	if (res == -1) return -1;

	if (stbuf.st_size == 0)
	{
		if (multithreaded)
			print_thread_pool.push(PrintErrorMessage, 0, new std::string(path.string()));
		else
			PrintErrorMessage(0, 0, new std::string(path.string()));
		return 0;
	}
	////fprintf(stderr, "To start11");
	//std::cout <<"stbuf.st_size=" << stbuf.st_size <<"::" << unit_size << '\n';

	size_t totalparts = stbuf.st_size / unit_size;
	////fprintf(stderr, "To start12");
	if ((stbuf.st_size - unit_size*totalparts) > 0)
		totalparts++;

	if (totalparts == 1)
	{
		if (multithreaded)
			upload_thread_pool.push(UploadFile, path, stbuf.st_size, fileNumber);
		else
			UploadFile(0, path, stbuf.st_size, fileNumber);
	}
	else
	{
		long unsigned int filehandle = open(path_c_str, O_RDONLY);

		fileInfo* fileinfo = new fileInfo();
		fileinfo->filehandle = filehandle;
		fileinfo->totalParts = totalparts;
		fileinfo->Succeeded_Parts = 0;
		fileinfo->filepath = boost::filesystem::path(path);
		fileinfo->filenumber = fileNumber;
		fileinfo->fileparts.reserve(totalparts);

		for (int i = 0; i < totalparts - 1; ++i)
		{
			partInfo* part = new partInfo();
			part->size = unit_size;
			part->parentfile = fileinfo;
			part->partlabel = new std::string("(" + std::to_string(i + 1) + " of " + std::to_string(totalparts) + ")");
			fileinfo->fileparts.push_back(part);

			std::function<void(int)>upload_part = std::bind(UploadPart, std::placeholders::_1, filehandle, (off_t)i*unit_size, (size_t)part->size, part);

			if (multithreaded)
				upload_thread_pool.push(upload_part);
			else
				upload_part(0);
		}

		partInfo* lastpart = new partInfo();

		lastpart->size = stbuf.st_size - (totalparts - 1)*unit_size;
		lastpart->parentfile = fileinfo;
		lastpart->partlabel = new std::string("(" + std::to_string(totalparts) + " of " + std::to_string(totalparts) + ")");
		fileinfo->fileparts.push_back(lastpart);

		std::function<void(int)>upload_lastpart = std::bind(UploadPart, std::placeholders::_1, filehandle, (off_t)(totalparts - 1)*unit_size, (size_t)lastpart->size, lastpart);

		if (multithreaded)
			upload_thread_pool.push(upload_lastpart);
		else
			upload_lastpart(0);
	}
}



int main(int argc, char* argv[]) {

	if (argc < 2)
	{
		std::string error = "\n============================\nPlease assign folder to be uploaded." + error + "\n============================\n";
		fprintf(stderr, error.c_str());
		return -1;
	}
	args = FUSE_ARGS_INIT(argc, argv);

	init_global_config(argv[1]); //"/home/yinglee/ofs";

	fuse_opt_parse(&args, &wof_global_conf, wof_opts, wof_opt_proc);

	std::string error = check_global_config();
	if (error != "")
	{
		error = "\n============================" + error + "\n============================\n";
		fprintf(stderr, error.c_str());
		return -1;
	}
	wos_global = wosapi::WosCluster::Connect("192.168.2.11");
	policy = wos_global->GetPolicy("all");
	path current_dir("."); //

	if (argc > 0)
		if (argv[1] != "")
			current_dir = path(argv[1]);

	size_t i = 0;

	if (wof_global_conf.uploadthreads <= 0)
		multithreaded = false;
	else
	{
		multithreaded = true;
		upload_thread_pool.resize(wof_global_conf.uploadthreads);
	}

	if (wof_global_conf.unit_size > 0)
		unit_size = wof_global_conf.unit_size * 1024 * 1024;
	else
		unit_size = 32 * 1024 * 1024;

	// Record start time
	auto start = std::chrono::system_clock::now();

	for (recursive_directory_iterator iter(current_dir), end;
		iter != end;
		++iter)
	{
		i++;
		processFile(iter->path(), i);
	}

	if (multithreaded)
		print_thread_pool.push(PrintMessage, new std::string("All " + std::to_string(i) + " files have been pushed into upload thread pool.\n"));
	else
		PrintMessage(0, new std::string("All " + std::to_string(i) + " files have been pushed into upload thread pool.\n"));

	if (multithreaded)
		upload_thread_pool.stop(true);
	//file_parts_thread_pool.stop(true);

	// Record end time
	auto finish = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed = finish - start;

	if (multithreaded)
		print_thread_pool.push(PrintMessage, new std::string("All files=" + std::to_string(i) + ".\n"
			+ "Elapsed time : " + std::to_string(elapsed.count()) + " s\n"));
	else
		PrintMessage(0, new std::string("All files=" + std::to_string(i) + ".\n"
			+ "Elapsed time : " + std::to_string(elapsed.count()) + " s\n"));

	if (multithreaded)
		print_thread_pool.stop(true);
}
