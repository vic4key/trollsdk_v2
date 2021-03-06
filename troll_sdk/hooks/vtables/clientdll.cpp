#include "../hooks.hpp"
#include "../../menu/menu.hpp"

void __stdcall hooks::clientdll::create_move::call( int sequence_number, float sample_frametime, bool active, bool& send_packet ) {
	o_create_move( sequence_number, sample_frametime, active, send_packet );

	auto cmd = i::input->get_user_cmd( sequence_number );
	auto verified = i::input->get_verified_user_cmd( sequence_number );

	if ( !cmd || !cmd->command_number || !i::engine->is_in_game( ) || !g_local || !g_local->is_alive( ) ) {
		return;
	}

	/* globals */
	g::cmd = cmd;
	exploit::tick_count = cmd->tick_count;
	g::send_packet = send_packet = true;

	/* get send_packet stuff */
	uintptr_t* framePtr; __asm mov framePtr, ebp;

	/* fix attack stuff */ {
		if ( !utils::can_shoot( ) || menu::opened ) {
			cmd->buttons &= ~in_attack;
		}
	}

	/* invalidate tickbase shift */
	exploit::tick_base_shift = 0;

	/* get viewangle before our prediction cuz prediction will change it */
	g::non_predicted_angle = cmd->viewangles;

	/* update prediction */
	engine_prediction::update( );

	/* prediction system related */ {
		engine_prediction::predict( cmd );

		/* predict our lby update */
		antiaim::predict_lby( );

		engine_prediction::restore( );
	}

	/* anti-untrsted */ {
		cmd->viewangles.clamp( );
	}

	/* get global angles */ {
		if ( g::send_packet ) {
			g::fake_angle = cmd->viewangles;
		}
		else {
			g::real_angle = cmd->viewangles;
		}
	}

	/* end our createmove */
	send_packet = g::send_packet;
	verified->m_cmd = *cmd;
	verified->m_crc = cmd->get_checksum( );
}

__declspec( naked ) void __fastcall hooks::clientdll::create_move::hook( void* ecx, void* edx, int sequence_number, float sample_frametime, bool active, bool& send_packet ) {
	__asm {
		push ebp
		mov  ebp, esp
		push ebx
		push esp
		push dword ptr[ active ]
		push dword ptr[ sample_frametime ]
		push dword ptr[ sequence_number ]
		call hooks::clientdll::create_move::call
		pop  ebx
		pop  ebp
		retn 0Ch
	}
}

void __fastcall hooks::clientdll::frame_stage_notify::hook( void* ecx, void* edx, int stage ) {
	if ( stage != frame_start ) {
		g::stage = stage;
	}

	if ( i::engine->is_in_game( ) && g_local ) { // so fsn has to be void*, int and if we push edx into stack we crash while returning our func ( the thing was here before )
		switch ( stage )
		{

		case frame_start:

			break;

		case frame_net_update_start:

			break;

		case frame_net_update_postdataupdate_start:

			break;

		case frame_net_update_postdataupdate_end:

			break;

		case frame_net_update_end:

			break;

		case frame_render_start:

			/* pvs fix */
			if ( g_local->is_alive( ) ) {
				for ( int i = 1; i < 65; i++ ) {
					auto pl = c_base_player::get_player_by_index( i );
					if ( !pl || !pl->is_player( ) || pl == g_local ) continue;

					*( int* ) ( ( uintptr_t ) pl + 0xA30 ) = i::globalvars->m_frame_count;
					*( int* ) ( ( uintptr_t ) pl + 0xA28 ) = 0;
				}
			}

			break;

		case frame_render_end:

			break;

		}
	}

	/* call og and do our features that needs to be done after */
	o_frame_stage_notify( ecx, 0, stage );

	/* shift rate */
	exploit::shift_rate = 100;
}

void write_cmd( bf_write* buf, c_usercmd* pin, c_usercmd* pout ) {
	static DWORD WriteUsercmdF = ( DWORD ) utils::find_sig_ida( "client.dll", "55 8B EC 83 E4 F8 51 53 56 8B D9 8B 0D" );
	__asm {
		mov     ecx, buf
		mov     edx, pin
		push	pout
		call    WriteUsercmdF
		add     esp, 4
	}
}

bool __fastcall hooks::clientdll::write_usercmd_delta_to_buffer::hook( void* ecx, void* edx, int slot, bf_write* buf, int from, int to, bool is_new_cmd ) {
	if ( exploit::tick_base_shift <= 0 )
		return o_write_usercmd_delta_to_buffer( ecx, 0, slot, buf, from, to, is_new_cmd );

	if ( !i::engine->is_in_game( ) || !g_local || !g_local->is_alive( ) ) {
		exploit::tick_base_shift = 0;
		return o_write_usercmd_delta_to_buffer( ecx, 0, slot, buf, from, to, is_new_cmd );
	}

	if ( from != -1 )
		return true;

	int* num_backup_commands = ( int* ) ( reinterpret_cast< uintptr_t >( buf ) - 0x30 );
	int* num_new_commands = ( int* ) ( reinterpret_cast< uintptr_t >( buf ) - 0x2C );

	int32_t new_commands = *num_new_commands;

	int32_t next_cmdnr = i::clientstate->m_last_outgoing_command + i::clientstate->m_choked_commands + 1;
	int32_t total_new_commands = min( new_commands + exploit::tick_base_shift, 62 );

	from = -1;
	*num_new_commands = total_new_commands;
	*num_backup_commands = 0;

	for ( to = next_cmdnr - new_commands + 1; to <= next_cmdnr; to++ ) {
		if ( !o_write_usercmd_delta_to_buffer( ecx, 0, slot, buf, from, to, true ) )
			return false;

		from = to;
	}

	c_usercmd* last_real_cmd = i::input->get_user_cmd( slot, from );

	if ( !last_real_cmd )
		return true;

	c_usercmd from_cmd, to_cmd;
	from_cmd = *last_real_cmd;
	to_cmd = from_cmd;

	to_cmd.command_number++;
	to_cmd.tick_count += exploit::shift_rate;

	for ( int i = new_commands; i <= total_new_commands; i++ ) {
		write_cmd( buf, &to_cmd, &from_cmd );
		from_cmd = to_cmd;
		to_cmd.command_number++;
		to_cmd.tick_count++;
	}

	return true;
}