#pragma once

namespace Render
{
	enum ERenderLockType
	{
		LOCK_DISCARD,
		LOCK_NOOVERWRITE,
		LOCK_DEFAULT,
		LOCK_READONLY,
		// Подсказка D3D9 (D3DLOCK_NOSYSLOCK): пропустить системный лок буфера.
		// Добавлена, чтобы вызовы, которые её запрашивали, не потеряли её при
		// переводе блокировок через IRenderBackend; остальные бэкенды её
		// игнорируют.
		LOCK_NOSYSLOCK,
	};
}; // namespace Render
