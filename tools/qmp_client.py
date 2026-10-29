"""Small local QMP client. Never logs command payloads (they may contain passwords)."""
import json
import socket
class QmpError(RuntimeError):
    pass
class QmpClient:
    def __init__(self,path,timeout=3):
        self.socket=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        self.socket.settimeout(timeout)
        try:
            self.socket.connect(str(path));self.stream=self.socket.makefile('rb')
            if 'QMP' not in self.read():raise QmpError('invalid QMP greeting')
            self.command('qmp_capabilities')
        except Exception:
            self.socket.close();raise
    def read(self):
        line=self.stream.readline(1048576)
        if not line:raise QmpError('emulator disconnected')
        return json.loads(line)
    def command(self,name,arguments=None):
        request={'execute':name}
        if arguments is not None:request['arguments']=arguments
        self.socket.sendall(json.dumps(request).encode()+b'\n')
        while True:
            reply=self.read()
            if 'error' in reply:raise QmpError('emulator rejected command')
            if 'return' in reply:return reply['return']
    def close(self):
        self.stream.close();self.socket.close()
    def __enter__(self):return self
    def __exit__(self,*_):self.close()
