#!/usr/bin/env node

const fs = require("fs");
const { spawn } = require('child_process');

const cc = 'clang++';
const cpp = "dyn_test.cpp"
var args = [cpp, '-I../source/'];

if (process.platform == "win32") {
	args = args.concat([
		'-o','dyn_test.dll'
	]);
} else {
	args = args.concat([
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