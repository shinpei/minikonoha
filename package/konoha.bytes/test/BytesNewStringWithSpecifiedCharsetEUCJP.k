/* written by shinpei nakata */

import("konoha.new");
import("konoha.bytes");
import("posix.file");

void test() {
	FILE f = System.fopen("files/eucjp.txt", "r");
	int size = 32;
	Bytes buf = new Bytes(size);
	buf.setAll(0);
	size = f.read(buf, 0, size);
	buf.set(size, 0);
	String newStr = new String(buf, 0, size, "EUC-JP");
	assert(newStr == "こんにちは世界\n");
}

test();
