#
# @TEST-EXEC: bro %INPUT >out
# @TEST-EXEC: btest-diff out

@TEST-START-FILE input.log
sdfkh:KH;fdkncv;ISEUp34:Fkdj;YVpIODhfDF
DSF"DFKJ"SDFKLh304yrsdkfj@#(*U$34jfDJup3UF
q3r3057fdf
sdfs\d

dfsdf
sdf
3rw43wRRERLlL#RWERERERE.
@TEST-END-FILE


module A;

export {
	redef enum Input::ID += { INPUT };
}

type Val: record {
	s: string;
};

event line(tpe: Input::Event, s: string) {
	print s;
}

event bro_init()
{
	Input::create_stream(A::INPUT, [$source="input.log", $reader=Input::READER_RAW, $mode=Input::STREAM]);
	Input::add_eventfilter(A::INPUT, [$name="input", $fields=Val, $ev=line]);
}
