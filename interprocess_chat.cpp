﻿#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/container/scoped_allocator.hpp>

class Chat 
{
private:

    using shared_memory_t    = boost::interprocess::managed_shared_memory;
    using manager_t          = shared_memory_t::segment_manager;
    using string_allocator_t = boost::interprocess::allocator < char, manager_t >;
    using string_t           = boost::interprocess::basic_string < char, std::char_traits < char >, string_allocator_t >;
    using vector_allocator_t = boost::container::scoped_allocator_adaptor < boost::interprocess::allocator < string_t, manager_t > >;
    //using vector_allocator_t = boost::interprocess::allocator < string_t, manager_t >;
    using vector_t           = boost::interprocess::vector < string_t, vector_allocator_t >;
    using mutex_t            = boost::interprocess::interprocess_mutex;
    using condition_t        = boost::interprocess::interprocess_condition;
    using counter_t          = std::atomic < std::size_t > ;

public:

    explicit Chat(const std::string & user_name):
        m_user_name(user_name),
        m_exit_flag(false),
        m_shared_memory(boost::interprocess::open_or_create, shared_memory_name.c_str(), 65536)
    {
        m_vector         = m_shared_memory.find_or_construct < vector_t > (m_vector_name.c_str()) (m_shared_memory.get_segment_manager());
        m_mutex          = m_shared_memory.find_or_construct < mutex_t > (m_mutex_name.c_str()) ();
        m_condition      = m_shared_memory.find_or_construct < condition_t > (m_condition_name.c_str()) ();
        m_users          = m_shared_memory.find_or_construct < counter_t > (m_users_name.c_str()) ();
        m_messages       = m_shared_memory.find_or_construct < counter_t > (m_messages_name.c_str()) ();
        
        ++(*m_users);
    }

    ~Chat() noexcept = default;

public:

    void run() 
    {
        auto reader = std::thread(&Chat::read, this);

        write();

        reader.join();

        send_message(m_user_name + " left the chat");

        --(*m_users);

        if (!(*m_users))
        {
            send_message("Chat is closed because everyone has left it.");
            std::cout << "Chat is closed because everyone has left it.\n";
            m_vector->clear();
            boost::interprocess::shared_memory_object::remove(shared_memory_name.c_str());
        }
    }

private:

    void read()
    {
        show_history();

        send_message(m_user_name + " joined the chat");

        while (true)
        {
            std::unique_lock < mutex_t > lock(*m_mutex);

            m_condition->wait(lock, [this]() 
            {
                if (this->m_mutex->try_lock())
                {
                    m_mutex->unlock();
                    return false;
                }
                else
                {
                    return true;
                }
            });

            if (*m_messages != m_local_messages)
            {
                std::cout << m_vector->back() << "\n";
                m_local_messages = *m_messages;
            }

            if (m_exit_flag)
            {
                break;
            }

        }
    }

    void show_history()
    {
        std::unique_lock < mutex_t > lock(*m_mutex);

        m_condition->wait(lock, [this]()
        {
            if (this->m_mutex->try_lock())
            {
                m_mutex->unlock();
                return false;
            }
            else
            {
                return true;
            }
        });

        std::cout << "Messenges' history:\n";
        for (const auto & message : *m_vector)
        {
            std::cout << message.c_str() << "\n";
        }
        std::cout << "\n";
    }

    void send_message(const std::string & message) 
    {
        m_vector->emplace_back(message.c_str());

        ++(*m_messages);

        m_local_messages = *m_messages;

        //if (*m_messages < max_vector_size)
          //  m_vector->emplace_back(message.c_str());
        //else
        //m_vector->emplace(std::next(std::begin(*m_vector), *m_messages), message.c_str());
    }

    void write()
    {
        while (!m_exit_flag)
        {
            std::string message;
            std::getline(std::cin, message);

            if (message != last)
            {
                std::unique_lock < mutex_t > lock(*m_mutex);
                send_message(m_user_name + ": " + message);
                m_condition->notify_all();
            }
            else
            {
                m_exit_flag = true;
            }
        }
    }

private:

    const std::string shared_memory_name = "shared_memory"; // maybe static?...
    const std::string m_vector_name      = "shred_history";
    const std::string m_mutex_name       = "shared_mutex";
    const std::string m_condition_name   = "shared_condtion";
    const std::string m_users_name       = "shared_users";
    const std::string m_messages_name    = "shared_messages";

    const std::string last               = "exit";
    //const std::size_t max_vector_size    = 10;

private:

    std::string m_user_name;

    std::atomic < bool > m_exit_flag;

    shared_memory_t m_shared_memory;

    vector_t    * m_vector;
    mutex_t     * m_mutex;
    condition_t * m_condition;
    counter_t   * m_users; // we should correctly see it
    counter_t   * m_messages;

    std::size_t m_local_messages;
};

int main(int argc, char ** argv) 
{
    std::string user_name;

    std::cout << "Enter your name: ";

    std::getline(std::cin, user_name);

    Chat(user_name).run();

    system("pause");

    return EXIT_SUCCESS;
}