#pragma once
// Cola entre la API pública, que se puede llamar desde cualquier hilo, y el hilo del juego.
#include <map>
#include <string>
#include <vector>

namespace api {

enum class RequestKind { Email, Text, Page, RunScript };

struct Request {
	RequestKind kind = RequestKind::Text;
	std::string from;     // Remitente del correo o del mensaje de busca.
	std::string subject;
	std::string body;     // Cuerpo del correo o texto del aviso.
	int notify = 0;

	// Correos con respuesta.
	std::string replyKey;
	std::string goodText;
	std::string badText;
	std::string goodScript;
	std::string badScript;
};

// Solo desde el hilo del juego.
std::vector<Request> Drain();
void SetUnreadCount(int count);

// Lo que ha respondido el jugador en esta partida, para EF_GetReply desde cualquier hilo.
void ClearReplies();
void SetReply(const std::string& key, int answer);

// Valores compartidos. La API los ve al momento; la partida los recoge en el hilo del juego.
bool IsValidValueKey(const std::string& key);
std::map<std::string, std::string> DrainValueChanges();
void ReplaceValues(const std::map<std::string, std::string>& values);
void SetValueFromGame(const std::string& key, const std::string& value);

}  // namespace api
