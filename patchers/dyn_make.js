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
		'-x', 'c++',
		'-std=c++11','-stdlib=libc++',
		'-arch','i386','-arch','x86_64',
		'-isysroot', '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.13.sdk',
		'-mmacosx-version-min=10.7',
		'-I../source/',
		'-I../../max-sdk/source/c74support/max-includes',
		'-I../../max-sdk/source/c74support/msp-includes',
		'-I../../max-sdk/source/c74support/jit-includes',
		'-F../../max-sdk/source/c74support/max-includes',
		'-F../../max-sdk/source/c74support/msp-includes',
		'-F../../max-sdk/source/c74support/jit-includes',
		'-dynamiclib', 
		'-undefined','dynamic_lookup',
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