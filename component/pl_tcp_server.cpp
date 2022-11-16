#include "pl_tcp_server.h"
#include "lwip/sockets.h"
#include "esp_check.h"

//==============================================================================

static const char* TAG = "pl_tcp_server";

//==============================================================================

namespace PL {

//==============================================================================

const TaskParameters TcpServer::defaultTaskParameters = {4096, tskIDLE_PRIORITY + 5, 0};

//==============================================================================

TcpServer::TcpServer (uint16_t port) : clientConnectedEvent (*this), clientDisconnectedEvent (*this), port (port) {}

//==============================================================================

TcpServer::~TcpServer() {
  if (status != Status::stopped) {
    status = Status::stopping;
    while (status == Status::stopping)
      vTaskDelay(1);
    for (auto& clientStream : clientStreams)
      clientStream->Close();
  }
}

//==============================================================================

esp_err_t TcpServer::Lock (TickType_t timeout) {
  esp_err_t error = mutex.Lock (timeout);
  if (error == ESP_OK)
    return ESP_OK;
  if (error == ESP_ERR_TIMEOUT && timeout == 0)
    return ESP_ERR_TIMEOUT;
  ESP_RETURN_ON_ERROR (error, TAG, "TCP server lock failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::Unlock() {
  ESP_RETURN_ON_ERROR (mutex.Unlock(), TAG, "TCP server unlock failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::Enable() {
  LockGuard lg (*this);
  if (status == Status::started)
    return ESP_OK;
  
  status = Status::starting;
  if (xTaskCreatePinnedToCore (TaskCode, GetName().c_str(), taskParameters.stackDepth, this, taskParameters.priority, NULL, taskParameters.coreId) != pdPASS) {
    status = Status::stopped;
    ESP_RETURN_ON_ERROR (ESP_FAIL, TAG, "TCP server create task failed");
  }
  while (status == Status::starting)
    vTaskDelay(1);
  ESP_RETURN_ON_FALSE (status == Status::started, ESP_FAIL, TAG, "TCP server enable failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::Disable() {
  LockGuard lg (*this);
  if (status == Status::stopped)
    return ESP_OK;

  status = Status::stopping;
  while (status == Status::stopping)
    vTaskDelay(1);
  for (auto& clientStream : clientStreams)
    clientStream->Close();
  clientStreams.clear();

  ESP_RETURN_ON_FALSE (status == Status::stopped, ESP_FAIL, TAG, "TCP server disable failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::EnableNagleAlgorithm() {
  LockGuard lg (*this);
  this->nagleAlgorithmEnabled = true;
  ESP_RETURN_ON_ERROR (SetStreamSocketOptions(), TAG, "stream socket options set failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::DisableNagleAlgorithm() {
  LockGuard lg (*this);
  this->nagleAlgorithmEnabled = false;
  ESP_RETURN_ON_ERROR (SetStreamSocketOptions(), TAG, "stream socket options set failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::EnableKeepAlive() {
  LockGuard lg (*this);
  this->keepAliveEnabled = true;
  ESP_RETURN_ON_ERROR (SetStreamSocketOptions(), TAG, "stream socket options set failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::DisableKeepAlive() {
  LockGuard lg (*this);
  this->keepAliveEnabled = false;
  ESP_RETURN_ON_ERROR (SetStreamSocketOptions(), TAG, "stream socket options set failed");
  return ESP_OK;
}


//==============================================================================

bool TcpServer::IsEnabled() {
  LockGuard lg (*this);
  return status == Status::started;
}

//==============================================================================

uint16_t TcpServer::GetPort() {
  LockGuard lg (*this);
  return port;
}

//==============================================================================

esp_err_t TcpServer::SetPort (uint16_t port) {
  LockGuard lg (*this);
  this->port = port;
  ESP_RETURN_ON_ERROR (RestartIfEnabled(), TAG, "TCP server restart failed");
  return ESP_OK;
}

//==============================================================================

size_t TcpServer::GetMaxNumberOfClients() {
  LockGuard lg (*this);
  return maxNumberOfClients;
}

//==============================================================================

esp_err_t TcpServer::SetMaxNumberOfClients(size_t maxNumberOfClients) {
  LockGuard lg (*this);
  this->maxNumberOfClients = maxNumberOfClients;
  ESP_RETURN_ON_ERROR (RestartIfEnabled(), TAG, "TCP server restart failed");
  return ESP_OK;
}

//==============================================================================

std::vector<std::shared_ptr<NetworkStream>> TcpServer::GetClientStreams() {
  LockGuard lg (*this);
  return clientStreams;  
}

//==============================================================================

esp_err_t TcpServer::SetTaskParameters (const TaskParameters& taskParameters) {
  LockGuard lg (*this);
  this->taskParameters = taskParameters;
  ESP_RETURN_ON_ERROR (RestartIfEnabled(), TAG, "TCP server restart failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::SetKeepAliveIdleTime (int seconds) {
  LockGuard lg (*this);
  this->keepAliveIdleTime = seconds;
  ESP_RETURN_ON_ERROR (RestartIfEnabled(), TAG, "TCP server restart failed");
  return ESP_OK;
  return SetStreamSocketOptions();
}

//==============================================================================

esp_err_t TcpServer::SetKeepAliveInterval (int seconds) {
  LockGuard lg (*this);
  this->keepAliveInterval = seconds;
  ESP_RETURN_ON_ERROR (SetStreamSocketOptions(), TAG, "stream socket options set failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::SetKeepAliveCount (int count) {
  LockGuard lg (*this);
  this->keepAliveCount = count;
  ESP_RETURN_ON_ERROR (SetStreamSocketOptions(), TAG, "stream socket options set failed");
  return ESP_OK;
}

//==============================================================================

esp_err_t TcpServer::SetStreamSocketOptions() {
  esp_err_t error = ESP_OK;
  for (auto& clientStream : clientStreams) {
    error = (nagleAlgorithmEnabled ? clientStream->EnableNagleAlgorithm() : clientStream->DisableNagleAlgorithm()) == ESP_OK ? error : ESP_FAIL;
    error = (keepAliveEnabled ? clientStream->EnableKeepAlive() : clientStream->DisableKeepAlive()) == ESP_OK ? error : ESP_FAIL;
    error = clientStream->SetKeepAliveIdleTime (keepAliveIdleTime) == ESP_OK ? error : ESP_FAIL;
    error = clientStream->SetKeepAliveInterval (keepAliveInterval) == ESP_OK ? error : ESP_FAIL;
    error = clientStream->SetKeepAliveCount (keepAliveCount) == ESP_OK ? error : ESP_FAIL;
  }
  ESP_RETURN_ON_ERROR (error, TAG, "stream socket options set failed");
  return ESP_OK;
}

//==============================================================================

void TcpServer::TaskCode (void* parameters) {
  TcpServer& server = *(TcpServer*)parameters;

  // Create listen socket
  int sock = -1;
  if ((sock = socket (AF_INET6, SOCK_STREAM, IPPROTO_TCP)) >= 0) {
    sockaddr_in6 addr = {};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons (server.port);
    if (bind (sock, (sockaddr*)&addr, sizeof (addr)) != 0 || listen (sock, server.maxNumberOfClients) != 0) {
      close (sock);
      sock = -1;
    }
  }

  if (sock >= 0) {
    server.status = Status::started;
    server.enabledEvent.Generate();

    while (server.status != Status::stopping) {
      if (server.Lock(0) == ESP_OK) {
        // Remove disconnected clients
        for (auto clientStream = server.clientStreams.begin(); clientStream != server.clientStreams.end();) {
          if ((*clientStream)->IsOpen())
            clientStream++;
          else {
            server.clientDisconnectedEvent.Generate (**clientStream);
            server.clientStreams.erase (clientStream);
          }
        }

        // Accept new clients
        fd_set set;
        timeval timeout = {};
        for (bool noPendingConnections = false; server.clientStreams.size() < server.maxNumberOfClients && !noPendingConnections;) {
          FD_ZERO (&set);
          FD_SET (sock, &set);
          if (select (sock + 1, &set, NULL, NULL, &timeout) > 0) {
            int newClientSock = accept (sock, NULL, NULL);
            if (newClientSock >= 0) {
              auto clientStream = std::make_shared<NetworkStream>(newClientSock);
              server.clientStreams.push_back (clientStream);
              server.SetStreamSocketOptions();
              server.clientConnectedEvent.Generate (*clientStream);
            }
          }
          else
            noPendingConnections = true;
        }

        // Handle requests
        for (auto& clientStream : server.clientStreams) {
          if (clientStream->GetReadableSize())
            server.HandleRequest (*clientStream);
        }

        server.Unlock();
      }
      vTaskDelay(1);
    }

    close (sock);
    server.status = Status::stopped;
    server.disabledEvent.Generate();
  }
  else
    server.status = Status::stopped;

  vTaskDelete (NULL);
}

//==============================================================================

esp_err_t TcpServer::RestartIfEnabled() {
  if (status == Status::stopped)
    return ESP_OK;
  ESP_RETURN_ON_ERROR (Disable(), TAG, "TCP server disable failed");
  ESP_RETURN_ON_ERROR (Enable(), TAG, "TCP server enable failed");
  return ESP_OK;
}

//==============================================================================

}