// See the file "COPYING" in the main distribution directory for copyright.

#include "config.h"

#ifdef USE_POSTGRES

#include <string>
#include <errno.h>

#include "../../NetVar.h"

#include "Postgres.h"
#include "../../threading/SerialTypes.h"

#include <vector>

using namespace logging;
using namespace writer;
using threading::Value;
using threading::Field;

Postgres::Postgres(WriterFrontend* frontend) : WriterBackend(frontend)
	{
	}

Postgres::~Postgres()
	{
		if ( conn != 0 )
			PQfinish(conn);
	}

string Postgres::GetTableType(int arg_type, int arg_subtype) {

	string type;

	switch ( arg_type ) {

	case TYPE_BOOL:
		type = "boolean";
		break;

	case TYPE_INT:
	case TYPE_COUNT:
	case TYPE_COUNTER:
	case TYPE_PORT:
		type = "integer";
		break;

		/*
	case TYPE_PORT:
		type = "VARCHAR(10)";
		break;
*/

	case TYPE_SUBNET:
	case TYPE_ADDR:
		type = "inet";
		break;

	case TYPE_TIME:
	case TYPE_INTERVAL:
	case TYPE_DOUBLE:
		type = "double precision";
		break;

	case TYPE_ENUM:
	case TYPE_STRING:
	case TYPE_FILE:
	case TYPE_FUNC:
		type = "TEXT";
		break;

	case TYPE_TABLE:
	case TYPE_VECTOR:
		type = GetTableType(arg_subtype, 0) + "[]";
		break;

	default:
		Error(Fmt("unsupported field format %d ", arg_type));
		return "";
	}

	return type;
}


bool Postgres::DoInit(const WriterInfo& info, int num_fields,
			    const Field* const * fields)
	{

	string hostname;
	map<string, string>::const_iterator it = info.config.find("hostname");
	if ( it == info.config.end() ) {
		MsgThread::Info(Fmt("hostname configuration option not found. Defaulting to localhost"));
		hostname = "localhost";
	} else {
		hostname = it->second;
	}
	

	const char *conninfo = Fmt("host = %s dbname = %s", hostname.c_str(), info.path.c_str());
	conn = PQconnectdb(conninfo);

	if ( PQstatus(conn) != CONNECTION_OK ) {
		printf("Could not connect to pg: %s\n", PQerrorMessage(conn));
		InternalError(Fmt("Could not connect to pg: %s", PQerrorMessage(conn)));
		assert(false);
	}

	string create = "CREATE TABLE IF NOT EXISTS test (\n"
		"id SERIAL UNIQUE NOT NULL";

	for ( int i = 0; i < num_fields; ++i )
		{
			const Field* field = fields[i];
			
			create += ",\n";

			create += field->name;

			string type = GetTableType(field->type, field->subtype);

			create += " "+type;
			/* if ( !field->optional ) {
				create += " NOT NULL";
			} */

		}

	create += "\n);";

	printf("Create: %s\n", create.c_str());
	PGresult *res = PQexec(conn, create.c_str());
	if ( PQresultStatus(res) != PGRES_COMMAND_OK) {
		printf("Command failed: %s\n", PQerrorMessage(conn));
		assert(false);
	}


	return true;
	}

bool Postgres::DoFlush()
	{
	return true;
	}

bool Postgres::DoFinish()
	{
	return true;
	}

// Format String
char* Postgres::FS(const char* format, ...) {
	char * buf;

	va_list al;
	va_start(al, format);
	int n = vasprintf(&buf, format, al);
	va_end(al);

	assert(n >= 0);

	return buf;
}

int Postgres::AddParams(Value* val, vector<char*> &params, string &call, int currId, bool addcomma)
	{

	if ( addcomma ) 
		call += ", ";

	if ( ! val->present )
		{
		call += "NULL";
		return currId;
		}

	switch ( val->type ) {

	case TYPE_BOOL:
		params.push_back(FS("%s", val->val.int_val ? "T" : "F"));
		call += Fmt("$%d", currId);
		return ++currId;

	case TYPE_INT:
		params.push_back(FS("%d", val->val.int_val));
		call += Fmt("$%d", currId);
		return ++currId;

	case TYPE_COUNT:
	case TYPE_COUNTER:
		params.push_back(FS("%d", val->val.uint_val));
		call += Fmt("$%d", currId);
		return ++currId;

	case TYPE_PORT:
		params.push_back(FS("%d", val->val.port_val.port));
		call += Fmt("$%d", currId);
		return ++currId;

	case TYPE_SUBNET:
		{
		call += Render(val->val.subnet_val);
		return ++currId;
		}

	case TYPE_ADDR:
		{
		call += Render(val->val.addr_val);
		return ++currId;
		}

	case TYPE_TIME:
	case TYPE_INTERVAL:
	case TYPE_DOUBLE:
		params.push_back(FS("%f", val->val.double_val));
		call += Fmt("$%d", currId);
		return ++currId;

	case TYPE_ENUM:
	case TYPE_STRING:
	case TYPE_FILE:
	case TYPE_FUNC:
		{
		if ( ! val->val.string_val->size() || val->val.string_val->size() == 0 ) {
			call += "NULL";
			return currId;
		}

		params.push_back(FS("%s", val->val.string_val->data()));
		call += Fmt("$%d", currId);
		return ++currId;
		}

	case TYPE_TABLE:
		{
		if ( ! val->val.set_val.size )
			{
			call += "NULL";
			return currId;
			}

		call += "ARRAY[";
		for ( int j = 0; j < val->val.set_val.size; j++ )
			{
			bool ac = true;
			if ( j == 0 )
				ac = false;

			currId = AddParams(val->val.set_val.vals[j], params, call, currId, ac);
			call += "::"+GetTableType(val->val.set_val.vals[j]->type, 0);
			}
		call += "]";

		return currId;
		}

	case TYPE_VECTOR:
		{
		if ( ! val->val.vector_val.size )
			{
			call += "NULL";
			return currId;
			}


		call += "ARRAY[";
		for ( int j = 0; j < val->val.vector_val.size; j++ )
			{
			bool ac = true;
			if ( j == 0 )
				ac = false;

			currId = AddParams(val->val.vector_val.vals[j], params, call, currId, ac);
			call += "::"+GetTableType(val->val.vector_val.vals[j]->type, 0);
			}
		call += "]";

		return currId;
		}

	default:
		Error(Fmt("unsupported field format %d", val->type ));
		return 0;
	}
	}

bool Postgres::DoWrite(int num_fields, const Field* const * fields, Value** vals)
	{
	vector<char*> params;
	
	string insert = "VALUES (";
	string names = "INSERT INTO test ( ";


	int currId = 1;
	for ( int i = 0; i < num_fields; i++ )
		{
			bool ac = true;

			if ( i == 0 ) {
				ac = false;
			} else {
				names += ", ";
			}

			currId = AddParams(vals[i], params, insert, currId, ac);
			names += fields[i]->name;

		}
	insert += ");";
	names += ") ";

	insert = names + insert;


	printf("Call: %s\n", insert.c_str());
	// & of vector is legal - according to current STL standard, vector has to be saved in consecutive memory.
	PGresult *res = PQexecParams(conn, insert.c_str(), params.size(), NULL, &params[0], NULL, NULL, 0);
	if ( PQresultStatus(res) != PGRES_COMMAND_OK) {
		printf("Command failed: %s\n", PQerrorMessage(conn));
		assert(false);
	}

	return true;
	}

bool Postgres::DoRotate(string rotated_path, double open, double close, bool terminating)
	{
	return true;
	}

bool Postgres::DoSetBuf(bool enabled)
	{
	// Nothing to do.
	return true;
	}

#endif /* USE_POSTGRES */
