// See the file "COPYING" in the main distribution directory for copyright.

#include "Raw.h"
#include "NetVar.h"

#include <fstream>
#include <sstream>

#include "../../threading/SerialTypes.h"

#define MANUAL 0
#define REREAD 1
#define STREAM 2

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace input::reader;
using threading::Value;
using threading::Field;

Raw::Raw(ReaderFrontend *frontend) : ReaderBackend(frontend)
{
	file = 0;

	//keyMap = new map<string, string>();
	
	separator.assign( (const char*) BifConst::InputRaw::record_separator->Bytes(), BifConst::InputRaw::record_separator->Len());
	if ( separator.size() != 1 ) {
		Error("separator length has to be 1. Separator will be truncated.");
	}

}

Raw::~Raw()
{
	DoFinish();
}

void Raw::DoFinish()
{
	if ( file != 0 ) {
		file->close();
		delete(file);
		file = 0;
	}
}

bool Raw::DoInit(string path, int arg_mode, int arg_num_fields, const Field* const* arg_fields)
{
	fname = path;
	mode = arg_mode;
	mtime = 0;
	
	if ( ( mode != MANUAL ) && (mode != REREAD) && ( mode != STREAM ) ) {
		Error(Fmt("Unsupported read mode %d for source %s", mode, path.c_str()));
		return false;
	} 	

	file = new ifstream(path.c_str());
	if ( !file->is_open() ) {
		Error(Fmt("Init: cannot open %s", fname.c_str()));
		return false;
	}
	
	num_fields = arg_num_fields;
	fields = arg_fields;

	if ( arg_num_fields != 1 ) {
		Error("Filter for raw reader contains more than one field. Filters for the raw reader may only contain exactly one string field. Filter ignored.");
		return false;
	}

	if ( fields[0]->type != TYPE_STRING ) {
		Error("Filter for raw reader contains a field that is not of type string.");
		return false;
	}

#ifdef DEBUG
	Debug(DBG_INPUT, "Raw reader created, will perform first update");
#endif

	switch ( mode ) {
		case MANUAL:
		case REREAD:
		case STREAM:
			DoUpdate();
			break;
		default:
			assert(false);
	}


	return true;
}


bool Raw::GetLine(string& str) {
	while ( getline(*file, str, separator[0]) ) {
		return true;
	}

	return false;
}


// read the entire file and send appropriate thingies back to InputMgr
bool Raw::DoUpdate() {
	switch ( mode ) {
		case REREAD:
			// check if the file has changed
			struct stat sb;
			if ( stat(fname.c_str(), &sb) == -1 ) {
				Error(Fmt("Could not get stat for %s", fname.c_str()));
				return false;
			}

			if ( sb.st_mtime <= mtime ) {
				// no change
				return true;
			}

			mtime = sb.st_mtime;
			// file changed. reread.

			// fallthrough
		case MANUAL:
		case STREAM:

			if ( file && file->is_open() ) {
				if ( mode == STREAM ) {
					file->clear(); // remove end of file evil bits
					break;
				}
				file->close();
			}
			file = new ifstream(fname.c_str());
			if ( !file->is_open() ) {
				Error(Fmt("cannot open %s", fname.c_str()));
				return false;
			}

			break;
		default:
			assert(false);

	}

	string line;
	while ( GetLine(line) ) {
		assert (num_fields == 1);
	
		Value** fields = new Value*[1];

		// filter has exactly one text field. convert to it.
		Value* val = new Value(TYPE_STRING, true);
		val->val.string_val = new string(line);
		fields[0] = val;
		
		Put(fields);
	}

	return true;
}


bool Raw::DoHeartbeat(double network_time, double current_time)
{
	ReaderBackend::DoHeartbeat(network_time, current_time);

	switch ( mode ) {
		case MANUAL:
			// yay, we do nothing :)
			break;
		case REREAD:
		case STREAM:
			Update(); // call update and not DoUpdate, because update actually checks disabled.
			break;
		default:
			assert(false);
	}

	return true;
}

