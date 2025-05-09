// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "IP_Packet.h"
#include "DEV9/PacketReader/NetLib.h"

#include "common/BitUtils.h"
#include "common/Console.h"

namespace PacketReader::IP
{
	int IP_Packet::GetHeaderLength() const
	{
		return headerLength;
	}

	u8 IP_Packet::GetDscpValue() const
	{
		return (dscp >> 2) & 0x3F;
	}
	void IP_Packet::SetDscpValue(u8 value)
	{
		dscp = (dscp & ~(0x3F << 2)) | ((value & 0x3F) << 2);
	}

	u8 IP_Packet::GetDscpECN() const
	{
		return dscp & 0x3;
	}
	void IP_Packet::SetDscpECN(u8 value)
	{
		dscp = (dscp & ~0x3) | (value & 0x3);
	}

	bool IP_Packet::GetDoNotFragment() const
	{
		return (fragmentFlags1 & (1 << 6)) != 0;
	}
	void IP_Packet::SetDoNotFragment(bool value)
	{
		fragmentFlags1 = (fragmentFlags1 & ~(0x1 << 6)) | ((value & 0x1) << 6);
	}

	bool IP_Packet::GetMoreFragments() const
	{
		return (fragmentFlags1 & (1 << 5)) != 0;
	}
	void IP_Packet::SetMoreFragments(bool value)
	{
		fragmentFlags1 = (fragmentFlags1 & ~(0x1 << 5)) | ((value & 0x1) << 5);
	}

	u16 IP_Packet::GetFragmentOffset() const
	{
		int x = 0;
		const u8 fF1masked = fragmentFlags1 & 0x1F;
		const u8 data[2] = {fF1masked, fragmentFlags2};
		u16 offset;
		NetLib::ReadUInt16(data, &x, &offset);
		return offset;
	}

	IP_Packet::IP_Packet(IP_Payload* data)
		: protocol{data->GetProtocol()}
		, payload{data}
	{
	}

	IP_Packet::IP_Packet(const u8* buffer, int bufferSize, bool fromICMP)
	{
		int offset = 0;

		//Bits 0-31
		u8 v_hl;
		NetLib::ReadByte08(buffer, &offset, &v_hl);
		headerLength = ((v_hl & 0xF) << 2);
		NetLib::ReadByte08(buffer, &offset, &dscp);

		u16 length;
		NetLib::ReadUInt16(buffer, &offset, &length);
		if (length > bufferSize)
		{
			if (!fromICMP)
				Console.Error("DEV9: IP_Packet: Unexpected Length");
			length = (u16)bufferSize;
		}

		//Bits 32-63
		NetLib::ReadUInt16(buffer, &offset, &id); //Send packets with unique IDs
		NetLib::ReadByte08(buffer, &offset, &fragmentFlags1);
		NetLib::ReadByte08(buffer, &offset, &fragmentFlags2);

		//Bits 64-95
		NetLib::ReadByte08(buffer, &offset, &timeToLive);
		NetLib::ReadByte08(buffer, &offset, &protocol);
		NetLib::ReadUInt16(buffer, &offset, &checksum);

		//Bits 96-127
		NetLib::ReadIPAddress(buffer, &offset, &sourceIP);
		//Bits 128-159
		NetLib::ReadIPAddress(buffer, &offset, &destinationIP);

		//Bits 160+
		if (headerLength > 20) //IP options (if any)
		{
			bool opReadFin = false;
			do
			{
				const u8 opKind = buffer[offset];
				const u8 opLen = buffer[offset + 1];
				switch (opKind)
				{
					case 0:
						opReadFin = true;
						break;
					case 1:
						options.push_back(new IPopNOP());
						offset += 1;
						continue;
					case 148:
						options.push_back(new IPopRouterAlert(buffer, offset));
						break;
					default:
						Console.Error("DEV9: IP_Packet: Got Unknown IP Option %d with len %d", opKind, opLen);
						options.push_back(new IPopUnk(buffer, offset));
						break;
				}
				offset += opLen;
				if (offset == headerLength)
					opReadFin = true;
			} while (opReadFin == false);
		}
		offset = headerLength;

		payload = std::make_unique<IP_PayloadPtr>(&buffer[offset], length - offset, protocol);
	}

	IP_Packet::IP_Packet(const IP_Packet& original)
		: headerLength{original.headerLength}
		, dscp{original.dscp}
		, id{original.id}
		, fragmentFlags1{original.fragmentFlags1}
		, fragmentFlags2{original.fragmentFlags2}
		, timeToLive{original.timeToLive}
		, protocol{original.protocol}
		, checksum{original.checksum}
		, sourceIP{original.sourceIP}
		, destinationIP{original.destinationIP}
		, payload{original.payload->Clone()}
	{
		//Clone options
		options.reserve(original.options.size());
		for (size_t i = 0; i < options.size(); i++)
			options.push_back(original.options[i]->Clone());
	}

	IP_Payload* IP_Packet::GetPayload() const
	{
		return payload.get();
	}

	int IP_Packet::GetLength()
	{
		ReComputeHeaderLen();
		return headerLength + payload->GetLength();
	}

	void IP_Packet::WriteBytes(u8* buffer, int* offset)
	{
		const int startOff = *offset;
		CalculateChecksum(); //ReComputeHeaderLen called in CalculateChecksum
		payload->CalculateChecksum(sourceIP, destinationIP);

		NetLib::WriteByte08(buffer, offset, (_verHi + (headerLength >> 2)));
		NetLib::WriteByte08(buffer, offset, dscp); //DSCP/ECN
		NetLib::WriteUInt16(buffer, offset, GetLength());

		NetLib::WriteUInt16(buffer, offset, id);
		NetLib::WriteByte08(buffer, offset, fragmentFlags1);
		NetLib::WriteByte08(buffer, offset, fragmentFlags2);

		NetLib::WriteByte08(buffer, offset, timeToLive);
		NetLib::WriteByte08(buffer, offset, protocol);
		NetLib::WriteUInt16(buffer, offset, checksum); //header csum

		NetLib::WriteIPAddress(buffer, offset, sourceIP);
		NetLib::WriteIPAddress(buffer, offset, destinationIP);

		//options
		for (size_t i = 0; i < options.size(); i++)
			options[i]->WriteBytes(buffer, offset);

		//Zero alignment bytes
		if (*offset != startOff + headerLength)
			memset(&buffer[*offset], 0, startOff + headerLength - *offset);

		*offset = startOff + headerLength;

		payload->WriteBytes(buffer, offset);
	}

	IP_Packet* IP_Packet::Clone() const
	{
		return new IP_Packet(*this);
	}

	void IP_Packet::ReComputeHeaderLen()
	{
		int opOffset = 20;
		for (size_t i = 0; i < options.size(); i++)
			opOffset += options[i]->GetLength();

		//needs to be a whole number of 32bits
		headerLength = Common::AlignUpPow2(opOffset, 4);
	}

	void IP_Packet::CalculateChecksum()
	{
		//if (!(i == 5)) //checksum field is 10-11th byte (5th short), which is skipped
		ReComputeHeaderLen();
		u8* headerSegment = new u8[headerLength];
		int counter = 0;
		NetLib::WriteByte08(headerSegment, &counter, (_verHi + (headerLength >> 2)));
		NetLib::WriteByte08(headerSegment, &counter, dscp); //DSCP/ECN
		NetLib::WriteUInt16(headerSegment, &counter, GetLength());

		NetLib::WriteUInt16(headerSegment, &counter, id);
		NetLib::WriteByte08(headerSegment, &counter, fragmentFlags1);
		NetLib::WriteByte08(headerSegment, &counter, fragmentFlags2);

		NetLib::WriteByte08(headerSegment, &counter, timeToLive);
		NetLib::WriteByte08(headerSegment, &counter, protocol);
		NetLib::WriteUInt16(headerSegment, &counter, 0); //header csum

		NetLib::WriteIPAddress(headerSegment, &counter, sourceIP);
		NetLib::WriteIPAddress(headerSegment, &counter, destinationIP);

		//options
		for (size_t i = 0; i < options.size(); i++)
			options[i]->WriteBytes(headerSegment, &counter);

		//Zero alignment bytes
		if (counter != headerLength)
			memset(&headerSegment[counter], 0, headerLength - counter);

		counter = headerLength;

		checksum = InternetChecksum(headerSegment, headerLength);
		delete[] headerSegment;
	}
	bool IP_Packet::VerifyChecksum()
	{
		ReComputeHeaderLen();
		u8* headerSegment = new u8[headerLength];
		int counter = 0;
		NetLib::WriteByte08(headerSegment, &counter, (_verHi + (headerLength >> 2)));
		NetLib::WriteByte08(headerSegment, &counter, dscp); //DSCP/ECN
		NetLib::WriteUInt16(headerSegment, &counter, GetLength());

		NetLib::WriteUInt16(headerSegment, &counter, id);
		NetLib::WriteByte08(headerSegment, &counter, fragmentFlags1);
		NetLib::WriteByte08(headerSegment, &counter, fragmentFlags2);

		NetLib::WriteByte08(headerSegment, &counter, timeToLive);
		NetLib::WriteByte08(headerSegment, &counter, protocol);
		NetLib::WriteUInt16(headerSegment, &counter, checksum); //header csum

		NetLib::WriteIPAddress(headerSegment, &counter, sourceIP);
		NetLib::WriteIPAddress(headerSegment, &counter, destinationIP);

		//options
		for (size_t i = 0; i < options.size(); i++)
			options[i]->WriteBytes(headerSegment, &counter);

		//Zero alignment bytes
		if (counter != headerLength)
			memset(&headerSegment[counter], 0, headerLength - counter);

		counter = headerLength;

		u16 csumCal = InternetChecksum(headerSegment, headerLength);
		delete[] headerSegment;

		return (csumCal == 0);
	}

	IP_Packet::~IP_Packet()
	{
		for (size_t i = 0; i < options.size(); i++)
			delete options[i];
	}

	u16 IP_Packet::InternetChecksum(const u8* buffer, int length)
	{
		//source http://stackoverflow.com/a/2201090

		int i = 0;
		u32 sum = 0;
		while (length > 1)
		{
			sum += ((u32)(buffer[i]) << 8) | ((u32)(buffer[i + 1]) & 0xFF);
			if ((sum & 0xFFFF0000) > 0)
			{
				sum &= 0xFFFF;
				sum += 1;
			}

			i += 2;
			length -= 2;
		}

		if (length > 0)
		{
			sum += (u32)(buffer[i] << 8);

			if ((sum & 0xFFFF0000) > 0)
			{
				sum = sum & 0xFFFF;
				sum += 1;
			}
		}
		sum = ~sum;
		sum = sum & 0xFFFF;
		return (u16)sum;
	}
} // namespace PacketReader::IP
