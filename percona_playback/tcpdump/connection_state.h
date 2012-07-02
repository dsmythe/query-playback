/* BEGIN LICENSE
 * Copyright (C) 2011 Percona Inc.
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 * END LICENSE */

#ifndef PERCONA_PLAYBACK_CONNECTION_STATE_H
#define PERCONA_PLAYBACK_CONNECTION_STATE_H

#include <stdlib.h>
#include <sys/time.h>
#include <vector>
#include <string>
#include <string.h>
#include <boost/shared_ptr.hpp>
#include <libdrizzle/drizzle_client.h>

#include "percona_playback/query_result.h"
#include "percona_playback/db_thread.h"
#include "tcpdump_mysql_parser_stats.h"

enum PktResult
{
 PKT_QUERY,
 PKT_RESULT,
 PKT_OK,
 PKT_UNKNOWN,
 PKT_ERROR
};

#define IN
#define OUT

#pragma pack(push, 1)
// Every logical MySQL packet starts w/ this 4 byte header
struct MysqlPkt {
  uint16_t  length_low;
  uint8_t   length_high;
  uint8_t   id;
  uint8_t   data[0];

  size_t data_length() const
  {
    return (size_t)length_low | ((size_t)length_high << 16);
  }

  size_t full_length() const
  {
    return data_length() + header_size();
  }

  static size_t header_size()
  {
    return offsetof(MysqlPkt, data);
  }
};
#pragma pack(pop)

struct AddrPort
{
  AddrPort() : address(0), port(0) {}
  AddrPort(uint32_t address, uint16_t port) : address(address), port(port) {}
  uint32_t address;
  uint16_t port;
  uint64_t ThreadId() const
  {
    return (uint64_t)address | ((uint64_t)port << 32);
  }
};

struct LastExecutedQueryInfo
{
  std::string   query;
  QueryResult   result;
  timeval       begin_pcap_timestamp;
  timeval       end_pcap_timestamp;
  timeval       end_timestamp;

  LastExecutedQueryInfo()
  {
    memset(&begin_pcap_timestamp, 0, sizeof(begin_pcap_timestamp));
    memset(&end_pcap_timestamp, 0, sizeof(end_pcap_timestamp));
    memset(&end_timestamp, 0, sizeof(end_timestamp));
  }
};


class ConnectionState : public DBThreadState

{

public:

  enum Origin
  {
    UNDEF,
    CLIENT,
    SERVER
  };

  ConnectionState() :
    current_origin(UNDEF),
    fragmented(false),
    handshake_from_client(false),
    was_query(false),
    eof_count(0),
    sent_rows_count(0),
    drizzle(drizzle_create(NULL), drizzle_free),
    drizzle_con(
      drizzle_con_add_tcp(drizzle.get(),
                          NULL,
                          "localhost",
                          3306,
                          "user",
                          "password",
                          "db",
                          (drizzle_con_options_t)
                            DRIZZLE_CON_MYSQL),
      drizzle_con_free),
    db_thread(NULL)
   {
     drizzle_con->result= NULL;
   }

  ~ConnectionState()
  {
    drizzle_result_free_all(drizzle_con.get());
  }

  void ProcessMysqlPkts(const u_char                 *pkts,
                        u_int                        total_len,
                        const timeval                &ts,
                        const AddrPort               &addr_port,
                        OUT TcpdumpMysqlParserStats  &stats);

  void ProcessFinishConnection();

  void SetCurrentOrigin(Origin o) { current_origin = o; }

  void SetDBThread(DBThread *t) { db_thread= t; }
  
  LastExecutedQueryInfo   last_executed_query_info;

private:

  typedef std::vector<unsigned char> UCharBuffer; 
  
  PktResult ParseMysqlPkt(IN UCharBuffer &buff, OUT std::string &query);

  PktResult ServerPacket(IN UCharBuffer &buff);
  PktResult ClientPacket(IN UCharBuffer &buff, OUT std::string &query);

  void DispatchQuery(const timeval      &ts,
                     const std::string  &query,
                     const AddrPort     &addr_port);

  void DispatchResult(const timeval     &ts,
                      const AddrPort    &addr_port);

  Origin            current_origin;
  bool              fragmented;
  UCharBuffer       frag_buff;
  bool              handshake_from_client;
  bool              was_query;
  size_t            eof_count;
  size_t            sent_rows_count;

  boost::shared_ptr<drizzle_st>       drizzle;
  boost::shared_ptr<drizzle_con_st>   drizzle_con;

  QueryResult       last_query_result;

  DBThread          *db_thread;

};

#endif // PERCONA_PLAYBACK_CONNECTION_STATE_H
