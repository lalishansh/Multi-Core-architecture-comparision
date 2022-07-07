#include <iostream>;
#include <windows.h> // for GetKeyState function
#include "manager.hxx";
#undef max
#undef min

int main() {
	union  {
		struct{
			uint32_t signalling, physics, render, postprocess, miscellaneous; // 35, 45, 65, 25, 40
		};
		struct {
			uint32_t all[5];
		};
	} engine_operations;

	Manager manager;

	manager.StartUp ();
	bool keep_running = true;
	while (keep_running) {
		static uint64_t itr = 0;
		itr++;
		do {
			std::cin.clear ();

			LOG_raw ("\nWeights [render, postprocess, physics, signal, miscellaneous] (all should be non-zero)\n> ");
			for (auto& optr: engine_operations.all) {
				std::cin >> optr;
			}
		} while (engine_operations.render == 0 || engine_operations.postprocess == 0 || engine_operations.physics == 0 || engine_operations.signalling == 0 || engine_operations.miscellaneous == 0);

		manager.Run (engine_operations.render, engine_operations.postprocess, engine_operations.physics, engine_operations.signalling, engine_operations.miscellaneous);
		while (true) {
			if (GetKeyState(VK_RETURN) < 0) {
				LOG_trace ("ReRunning With Random Params #{}", itr);
				break;
			}
			else if (GetKeyState(VK_ESCAPE) < 0) {
				keep_running = false;
				break;
			}
			std::this_thread::sleep_for (std::chrono::milliseconds(100));
		}
	}

	return 0;
}
