#pragma once
// Cola entre la API pública, que se puede llamar desde cualquier hilo, y el hilo del juego.
#include <string>
#include <vector>

namespace api {

enum class RequestKind { Email, Text, Page };

struct Request {
	RequestKind kind = RequestKind::Text;
	std::string from;     // Remitente del correo o del mensaje de busca.
	std::string subject;
	std::string body;     // Cuerpo del correo o texto del aviso.
	int notify = 0;
};

// Solo desde el hilo del juego.
std::vector<Request> Drain();
void SetUnreadCount(int count);

}  // namespace api
