/*
Minetest-c55
Copyright (C) 2010 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "socket.h"
#include "debug.h"
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include "utility.h"

// Debug printing options
#define DP 0
#define DPS ""

bool g_sockets_initialized = false;

struct VirtualPacket {
	Address sender;
	std::string data;
};

class VirtualNetwork {
public:
	static VirtualNetwork& getInstance() {
		static VirtualNetwork instance;
		return instance;
	}
	
	void sendPacket(unsigned short dest_port, const Address &sender, const void *data, int size) {
		JMutexAutoLock lock(m_mutex);
		m_queues[dest_port].push_back({sender, std::string((const char*)data, size)});
	}
	
	bool receivePacket(unsigned short port, Address &sender, void *data, int size, int &received_size) {
		JMutexAutoLock lock(m_mutex);
		std::map<unsigned short, std::list<VirtualPacket> >::iterator it = m_queues.find(port);
		if (it != m_queues.end() && !it->second.empty()) {
			VirtualPacket p = it->second.front();
			it->second.pop_front();
			sender = p.sender;
			received_size = (p.data.size() < (size_t)size) ? p.data.size() : size;
			memcpy(data, p.data.c_str(), received_size);
			return true;
		}
		return false;
	}
	
	bool hasData(unsigned short port) {
		JMutexAutoLock lock(m_mutex);
		std::map<unsigned short, std::list<VirtualPacket> >::iterator it = m_queues.find(port);
		return it != m_queues.end() && !it->second.empty();
	}
	
private:
	VirtualNetwork() { m_mutex.Init(); }
	JMutex m_mutex;
	std::map<unsigned short, std::list<VirtualPacket> > m_queues;
};

void sockets_init()
{
	g_sockets_initialized = true;
}

void sockets_cleanup()
{
}

Address::Address()
{
	m_address = 0;
	m_port = 0;
}

Address::Address(unsigned int address, unsigned short port)
{
	m_address = address;
	m_port = port;
}

Address::Address(unsigned int a, unsigned int b,
		unsigned int c, unsigned int d,
		unsigned short port)
{
	m_address = (a<<24) | (b<<16) | ( c<<8) | d;
	m_port = port;
}

bool Address::operator==(Address &address)
{
	return (m_address == address.m_address
			&& m_port == address.m_port);
}

bool Address::operator!=(Address &address)
{
	return !(*this == address);
}

void Address::Resolve(const char *name)
{
	m_address = 0x7F000001; // Always resolve to 127.0.0.1 (loopback) in virtual network
}

std::string Address::serializeString() const
{
	unsigned int a, b, c, d;
	a = (m_address & 0xFF000000)>>24;
	b = (m_address & 0x00FF0000)>>16;
	c = (m_address & 0x0000FF00)>>8;
	d = (m_address & 0x000000FF);
	return itos(a)+"."+itos(b)+"."+itos(c)+"."+itos(d);
}

unsigned int Address::getAddress() const
{
	return m_address;
}

unsigned short Address::getPort() const
{
	return m_port;
}

void Address::setAddress(unsigned int address)
{
	m_address = address;
}

void Address::setAddress(unsigned int a, unsigned int b,
		unsigned int c, unsigned int d)
{
	m_address = (a<<24) | (b<<16) | ( c<<8) | d;
}

void Address::setPort(unsigned short port)
{
	m_port = port;
}

void Address::print(std::ostream *s) const
{
	(*s)<<((m_address>>24)&0xff)<<"."
			<<((m_address>>16)&0xff)<<"."
			<<((m_address>>8)&0xff)<<"."
			<<((m_address>>0)&0xff)<<":"
			<<m_port;
}

void Address::print() const
{
	print(&dstream);
}

UDPSocket::UDPSocket()
{
	if(g_sockets_initialized == false)
		throw SocketException("Sockets not initialized");
	
	m_handle = 1; // Dummy handle
	m_bound_port = 0;
	setTimeoutMs(0);
}

UDPSocket::~UDPSocket()
{
}

void UDPSocket::Bind(unsigned short port)
{
	if(DP)
		dstream<<DPS<<"UDPSocket::Bind(): port="<<port<<std::endl;
	
	if (port == 0) {
		static unsigned short next_port = 50000;
		m_bound_port = next_port++;
	} else {
		m_bound_port = port;
	}
}

void UDPSocket::Send(const Address & destination, const void * data, int size)
{
	if (m_bound_port == 0) {
		Bind(0);
	}
	
	Address sender(0x7F000001, m_bound_port);
	VirtualNetwork::getInstance().sendPacket(destination.getPort(), sender, data, size);
}

int UDPSocket::Receive(Address & sender, void * data, int size)
{
	if(WaitData(m_timeout_ms) == false)
	{
		return -1;
	}

	int received_size = 0;
	if (VirtualNetwork::getInstance().receivePacket(m_bound_port, sender, data, size, received_size)) {
		return received_size;
	}
	return -1;
}

int UDPSocket::GetHandle()
{
	return m_handle;
}

void UDPSocket::setTimeoutMs(int timeout_ms)
{
	m_timeout_ms = timeout_ms;
}

bool UDPSocket::WaitData(int timeout_ms)
{
	if (m_bound_port == 0)
		return false;

	if (VirtualNetwork::getInstance().hasData(m_bound_port))
		return true;

	if (timeout_ms > 0) {
		// Very simple sleep for virtual network timeout
		sleep_ms(timeout_ms);
		return VirtualNetwork::getInstance().hasData(m_bound_port);
	}
	return false;
}
