#include <algorithm>
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
#include <boost/circular_buffer.hpp>

class Chat
{
private:

    using shared_memory_t = boost::interprocess::managed_shared_memory;
    using manager_t = shared_memory_t::segment_manager;
    using string_allocator_t = boost::interprocess::allocator < char, manager_t >;
    using string_t = boost::interprocess::basic_string < char, std::char_traits < char >, string_allocator_t >;
    using vector_allocator_t = boost::container::scoped_allocator_adaptor < boost::interprocess::allocator < string_t, manager_t > >;
    using vector_t = boost::interprocess::vector < string_t, vector_allocator_t >;
    using circular_buffer_allocator_t = boost::container::scoped_allocator_adaptor < boost::interprocess::allocator < string_t, manager_t > >;
    using circular_buffer_t = boost::circular_buffer < string_t, circular_buffer_allocator_t >;
    using mutex_t = boost::interprocess::interprocess_mutex;
    using condition_t = boost::interprocess::interprocess_condition;
    using counter_t = std::atomic < std::size_t >;

public:

    explicit Chat(const std::string& user_name) :
        m_user_name(user_name),
        m_exit_flag(false),
        m_shared_memory(boost::interprocess::open_or_create, shared_memory_name.c_str(), 65536)
    {
        m_vector = m_shared_memory.find_or_construct < vector_t >(m_vector_name.c_str()) (m_shared_memory.get_segment_manager());
        m_cb = m_shared_memory.find_or_construct < circular_buffer_t >(m_cb_name.c_str()) (max_cb_size, m_shared_memory.get_segment_manager());
        m_mutex = m_shared_memory.find_or_construct < mutex_t >(m_mutex_name.c_str()) ();
        m_condition = m_shared_memory.find_or_construct < condition_t >(m_condition_name.c_str()) ();
        m_users = m_shared_memory.find_or_construct < counter_t >(m_users_name.c_str()) ();
        m_messages = m_shared_memory.find_or_construct < counter_t >(m_messages_name.c_str()) ();
        
        m_local_messages = *m_messages;

        ++(*m_users);
    }

    ~Chat() noexcept = default;

public:

    void run()
    {
        auto reader = std::thread(&Chat::read, this);

        write();

        m_exit_flag = true;

        send_message(m_user_name + " left the chat");

        reader.join();

        if (!(--(*m_users)))
        {
            send_message("Chat is closed because everyone has left it.");
            std::cout << "\nChat is closed because everyone has left it.\n";
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
                return m_local_messages != *m_messages || m_exit_flag;
            });

            if (m_exit_flag)
            {
                break;
            }
            
            m_local_messages = *m_messages;

            std::cout << m_vector->back() << "\n";
        }
    }

    void show_history()
    {
        std::unique_lock < mutex_t > lock(*m_mutex);

        std::cout << "\nAccessible messenges' history:\n";
        for (auto const & record : *m_vector)
        {
                std::cout << record << "\n";
        }
        std::cout << "\n";
    }

    void send_message(const std::string& message)
    {
        std::unique_lock < mutex_t > lock(*m_mutex);

        (*m_messages)++;

        m_local_messages++;

        m_vector->emplace_back(message.c_str());

        m_condition->notify_all();
    }

    void write()
    {
        for (
            std::string message;
            std::getline(std::cin, message) && (message != last);
            send_message(m_user_name + ": " + message)
        );
    }

private:

    inline static const std::string shared_memory_name = "shared_memory";
    inline static const std::string m_vector_name = "shared_full_history";
    inline static const std::string m_cb_name = "shared_accessible_history";
    inline static const std::string m_mutex_name = "shared_mutex";
    inline static const std::string m_condition_name = "shared_condtion";
    inline static const std::string m_users_name = "shared_users";
    inline static const std::string m_messages_name = "shared_messages";

    inline static const std::string last = "exit";
    inline static const std::size_t max_vector_size = 10;
    inline static const std::size_t max_cb_size = 10;

private:

    std::string m_user_name;

    std::atomic < bool > m_exit_flag;

    shared_memory_t m_shared_memory;

    vector_t* m_vector;
    circular_buffer_t* m_cb;
    mutex_t* m_mutex;
    condition_t* m_condition;
    counter_t* m_users;
    counter_t* m_messages;

    std::size_t m_local_messages;
};

int main(int argc, char** argv)
{
    std::string user_name;

    std::cout << "Enter your name: ";

    std::getline(std::cin, user_name);

    Chat(user_name).run();

    system("pause");

    return EXIT_SUCCESS;
}