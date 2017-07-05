#!/usr/bin/env node

const fs = require("fs");
const { exec, spawn } = require('child_process');

var cc;
const cpp = "dyn_test.cpp"
var args = [cpp];

if (process.platform == "win32") {

	// didn't figure this out yet, but have a look at dyn_make.bat
	// anyway filewatching isn't super feasible yet either
	// because Windows won't let you replace a dll that is currently linked
	
} else {
	cc = 'clang++';
	args = args.concat([
		'-I../source/',
		'-dynamiclib', 
		'-undefined','dynamic_lookup',
		'-arch','i386','-arch','x86_64',
		'-o','dyn_test.dylib'
	]);
}


function make() {
	
	console.log(cc, args.join(" "));
	const cmd = spawn(cc, args, {stdio: "inherit"});
	cmd.on('close', (code) => {
		//console.log(`${cc} exited with code ${code}`);
	});
}

// using fs.watchFile because fs.watch fires too many events
var watcher1 = fs.watchFile(cpp, make)

make();