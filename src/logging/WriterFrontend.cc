
#include "Net.h"
#include "threading/SerialTypes.h"

#include "Manager.h"
#include "WriterFrontend.h"
#include "WriterBackend.h"

using threading::Value;
using threading::Field;

namespace logging  {

// Messages sent from frontend to backend (i.e., "InputMessages").

class InitMessage : public threading::InputMessage<WriterBackend>
{
public:
	InitMessage(WriterBackend* backend, const WriterBackend::WriterInfo& info, const int num_fields, const Field* const* fields, const string& frontend_name)
		: threading::InputMessage<WriterBackend>("Init", backend),
		info(info), num_fields(num_fields), fields(fields),
		frontend_name(frontend_name) { }

	virtual bool Process() { return Object()->Init(info, num_fields, fields, frontend_name); }

private:
	WriterBackend::WriterInfo info;
	const int num_fields;
	const Field * const* fields;
	const string frontend_name;
};

class RotateMessage : public threading::InputMessage<WriterBackend>
{
public:
	RotateMessage(WriterBackend* backend, WriterFrontend* frontend, const string rotated_path, const double open,
		      const double close, const bool terminating)
		: threading::InputMessage<WriterBackend>("Rotate", backend),
		frontend(frontend),
		rotated_path(rotated_path), open(open),
		close(close), terminating(terminating) { }

	virtual bool Process() { return Object()->Rotate(rotated_path, open, close, terminating); }

private:
	WriterFrontend* frontend;
	const string rotated_path;
	const double open;
	const double close;
	const bool terminating;
};

class WriteMessage : public threading::InputMessage<WriterBackend>
{
public:
	WriteMessage(WriterBackend* backend, int num_fields, int num_writes, Value*** vals)
		: threading::InputMessage<WriterBackend>("Write", backend),
		num_fields(num_fields), num_writes(num_writes), vals(vals)	{}

	virtual bool Process() { return Object()->Write(num_fields, num_writes, vals); }

private:
	int num_fields;
	int num_writes;
	Value ***vals;
};

class SetBufMessage : public threading::InputMessage<WriterBackend>
{
public:
	SetBufMessage(WriterBackend* backend, const bool enabled)
		: threading::InputMessage<WriterBackend>("SetBuf", backend),
		enabled(enabled) { }

	virtual bool Process() { return Object()->SetBuf(enabled); }

private:
	const bool enabled;
};

class FlushMessage : public threading::InputMessage<WriterBackend>
{
public:
	FlushMessage(WriterBackend* backend)
		: threading::InputMessage<WriterBackend>("Flush", backend)	{}

	virtual bool Process() { return Object()->Flush(); }
};

class FinishMessage : public threading::InputMessage<WriterBackend>
{
public:
	FinishMessage(WriterBackend* backend)
		: threading::InputMessage<WriterBackend>("Finish", backend)	{}

	virtual bool Process() { return Object()->DoFinish(); }
};

}

// Frontend methods.

using namespace logging;

WriterFrontend::WriterFrontend(EnumVal* arg_stream, EnumVal* arg_writer, bool arg_local, bool arg_remote)
	{
	stream = arg_stream;
	writer = arg_writer;
	Ref(stream);
	Ref(writer);

	disabled = initialized = false;
	buf = true;
	local = arg_local;
	remote = arg_remote;
	write_buffer = 0;
	write_buffer_pos = 0;
	ty_name = "<not set>";

	if ( local )
		{
		backend = log_mgr->CreateBackend(this, writer->AsEnum());

		if ( backend )
			backend->Start();
		}

	else
		backend = 0;
	}

WriterFrontend::~WriterFrontend()
	{
	Unref(stream);
	Unref(writer);
	}

string WriterFrontend::Name() const
	{
	if ( ! info.path.size() )
		return ty_name;

	return ty_name + "/" + info.path;
	}

void WriterFrontend::Stop()
	{
	FlushWriteBuffer();
	SetDisable();

	if ( backend )
		backend->Stop();
	}

void WriterFrontend::Init(const WriterBackend::WriterInfo& arg_info, int arg_num_fields, const Field* const * arg_fields)
	{
	if ( disabled )
		return;

	if ( initialized )
		reporter->InternalError("writer initialize twice");

	info = arg_info;
	num_fields = arg_num_fields;
	fields = arg_fields;

	initialized = true;

	if ( backend )
		backend->SendIn(new InitMessage(backend, arg_info, arg_num_fields, arg_fields, Name()));

	if ( remote )
		remote_serializer->SendLogCreateWriter(stream,
						       writer,
						       arg_info,
						       arg_num_fields,
						       arg_fields);

	}

void WriterFrontend::Write(int num_fields, Value** vals)
	{
	if ( disabled )
		return;

	if ( remote )
		remote_serializer->SendLogWrite(stream,
						writer,
						info.path,
						num_fields,
						vals);

	if ( ! backend )
		{
		DeleteVals(vals);
		return;
		}

	if ( ! write_buffer )
		{
		// Need new buffer.
		write_buffer = new Value**[WRITER_BUFFER_SIZE];
		write_buffer_pos = 0;
		}

	write_buffer[write_buffer_pos++] = vals;

	if ( write_buffer_pos >= WRITER_BUFFER_SIZE || ! buf || terminating )
		// Buffer full (or no bufferin desired or termiating).
		FlushWriteBuffer();

	}

void WriterFrontend::FlushWriteBuffer()
	{
	if ( ! write_buffer_pos )
		// Nothing to do.
		return;

	if ( backend )
		backend->SendIn(new WriteMessage(backend, num_fields, write_buffer_pos, write_buffer));

	// Clear buffer (no delete, we pass ownership to child thread.)
	write_buffer = 0;
	write_buffer_pos = 0;
	}

void WriterFrontend::SetBuf(bool enabled)
	{
	if ( disabled )
		return;

	buf = enabled;

	if ( backend )
		backend->SendIn(new SetBufMessage(backend, enabled));

	if ( ! buf )
		// Make sure no longer buffer any still queued data.
		FlushWriteBuffer();
	}

void WriterFrontend::Flush()
	{
	if ( disabled )
		return;

	FlushWriteBuffer();

	if ( backend )
		backend->SendIn(new FlushMessage(backend));
	}

void WriterFrontend::Rotate(string rotated_path, double open, double close, bool terminating)
	{
	if ( disabled )
		return;

	FlushWriteBuffer();

	if ( backend )
		backend->SendIn(new RotateMessage(backend, this, rotated_path, open, close, terminating));
	else
		// Still signal log manager that we're done, but signal that
		// nothing happened by setting the writer to zeri.
		log_mgr->FinishedRotation(0, "", rotated_path, open, close, terminating);
	}

void WriterFrontend::Finish()
	{
	if ( disabled )
		return;

	FlushWriteBuffer();

	if ( backend )
		backend->SendIn(new FinishMessage(backend));
	}

void WriterFrontend::DeleteVals(Value** vals)
	{
	// Note this code is duplicated in Manager::DeleteVals().
	for ( int i = 0; i < num_fields; i++ )
		delete vals[i];

	delete [] vals;
	}
