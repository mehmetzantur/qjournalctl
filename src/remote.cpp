#include "remote.h"
#include "passworddialog.h"
#include "error.h"

#include <QProcess>
#include <QDebug>

Remote::Remote(QObject *qObject, QString hostnameString, QString usernameString)
{
	ssh = ssh_new();
	assert(ssh != nullptr);

	ssh_options_set(ssh, SSH_OPTIONS_HOST, "localhost");
	int ok;

	ok = ssh_connect(ssh);
    if(ok != SSH_OK){
        ssh_free(ssh);
        throw new Error("Establishing a connection to the remote host failed. Please try again!");
    }

	unsigned char *hash = nullptr;
	ssh_key srv_pubkey = nullptr;
	size_t hlen;

	ok = ssh_get_server_publickey(ssh, &srv_pubkey);
	assert(ok>=0);

    // Todo: Provide better case destinction
	ok = ssh_session_is_known_server(ssh);
    if(ok != SSH_KNOWN_HOSTS_OK){
        ssh_free(ssh);
        throw new Error("Remote server is either unknown, unavailable, or has changed its public keys. "
                        "Please verify your connection manually, adjust your .ssh/known_hosts file "
                        "and come back!");
    }


	// Get the required password
	QString passwordString;
	PasswordDialog passwordDialog(nullptr, &passwordString);
	passwordDialog.exec();

	const char *password = passwordString.toUtf8().data();
	const char *username = usernameString.toUtf8().data();

	ok = ssh_userauth_password(ssh, username, password);
    if(ok != SSH_AUTH_SUCCESS){
        throw new Error("SSH Authentication on the remote host failed. Please try again!");
    }

	qDebug() << "Authenticated :)";

    initSSHChannel();

	// Reader thread
	destroyAllThreads = false;
	sshCmd = "";

	readerThread = new std::thread([&]() {

		char buffer[8192];

		while(!destroyAllThreads){
            usleep(50000); // todo decrease this, investigate why it crashes < 100000

            sshMutex.lock();
			if(sshCmd != ""){
				char *data = sshCmd.toUtf8().data();
                ssh_channel_request_exec(sshChannel, data);
                //ssh_channel_write(sshChannel, data, strlen(data));
            }

            // Reset ssh stuff
            sshCmd = "";

			assert(!ssh_channel_is_eof(sshChannel));
			assert(ssh_channel_is_open(sshChannel));

            int bytesRead = ssh_channel_read_nonblocking(sshChannel, buffer, 8192, 0);
            sshMutex.unlock();

            if(bytesRead > 0){
                buffer[bytesRead] = '\0';
                QString dataString(buffer);

                emit remoteDataAvailable(dataString);
            }
		}
	});
}

Remote::~Remote()
{
	destroyAllThreads = true;
	readerThread->join();

    close();

	ssh_disconnect(ssh);
	ssh_free(ssh);
}


void Remote::initSSHChannel()
{
    sshChannel = ssh_channel_new(ssh);
    assert(sshChannel != nullptr);

    int ok;
    ok = ssh_channel_open_session(sshChannel);
    assert(ok == SSH_OK);

    //ok = ssh_channel_request_pty_size(sshChannel, "v100", 100, 40);
    assert(ok == SSH_OK);

    //ok = ssh_channel_request_shell(sshChannel);
    assert(ok == SSH_OK);

}


void Remote::run(QString cmd)
{
	sshMutex.lock();
    if(isRunning()){
        ssh_channel_close(sshChannel);
        ssh_channel_free(sshChannel);
    }

    initSSHChannel();

	sshCmd = cmd + "\n";
	sshMutex.unlock();
}

void Remote::close()
{
	sshMutex.lock();

    sshCmd = "";
    ssh_channel_close(sshChannel);
    ssh_channel_free(sshChannel);

	sshMutex.unlock();
}


bool Remote::isRunning()
{
    return ssh_channel_is_open(sshChannel);
}


void Remote::processHasData()
{
	QString readString = "\n";
	readString = readString.left(readString.size()-1);

	emit remoteDataAvailable(readString);
}
