#spill

##NAME

spill - spill pipeline to disk if write would block

##SYNOPSIS

	spill file

##DESCRIPTION

spill reads from stdin and writes to stdout. If a write would block, divert the
write through file.

##OPTIONS

	-m, --memory=BYTES

Use up to BYTES bytes of memory for data before writing to file.

	-s, --size=BYTES

Do not allow file to grow larger than BYTES bytes.

	-b, --block

Block if file would grow larger than the size specified with -s.

	-a, --abort

Abort if file would grow larger than the size specified with -s.

##SEE ALSO

mkfifo(1), mktemp(1), sponge(1), tee(1)
