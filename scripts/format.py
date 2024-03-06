import os, sys, subprocess
from pathlib import Path

def is_cpp(filename):
	return Path(filename).suffix in ['.cpp', '.hpp', '.cxx', '.c', '.h']


def run_command(filename):
	cmd_list = ['clang-format-15', '-i', filename]
	p = subprocess.run(cmd_list, capture_output=True)
	if p.returncode != 0:
		raise ValueError(p.stderr.decode('utf-8'))
#	print(filename)


def format_file(root: str)->None:
	exclude_dir = ['external', '.git', '.idea', 'cmake-build-debug', 'scripts']
	full_path = os.path.realpath(root)
	for directory in os.listdir(full_path):
		path = os.path.join(full_path, directory)
		if (directory in exclude_dir) or directory.startswith('.'):
			continue
		if os.path.isdir(path):
			for p in Path(path).rglob('*'):
				p = os.path.realpath(p)
				if (not os.path.isdir(p)) and is_cpp(p):
					run_command(p)
		else:
			run_command(p)

if __name__ == '__main__':
	if len(sys.argv) == 2:
		format_file(sys.argv[1])
