
var bigdict = new Dict("bigdict");

function bang() {
	bigdict.clear();
	var s = "";
	for (var i=0; i<64*128; i++) {
		bigdict.set("var"+i, i);
	}
	
	s = bigdict.stringify();
	
	post("js made string of", s.length, "\n");
	
	outlet(0, "bang");
}

function received(s) {
	post("js received string of", s.length, "\n");
}