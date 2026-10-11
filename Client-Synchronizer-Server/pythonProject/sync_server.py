from waitress import serve
from client_sync import app

if __name__ == '__main__':
    serve(app, host='0.0.0.0', port='27302', threads=24)