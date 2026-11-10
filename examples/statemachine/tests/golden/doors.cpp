#include <iostream>
#include <map>
#include <string>

class Doors;

class State {
protected:
    Doors *statemachine;

public:
    virtual ~State() {}

    void set_context(Doors *statemachine) {
        this->statemachine = statemachine;
    }

    virtual std::string get_name() {
        return "Unknown";
    }

    virtual void open() {
        std::cout << "Impossible event for the current state." << std::endl;
    }

    virtual void close() {
        std::cout << "Impossible event for the current state." << std::endl;
    }
};


class Doors {
private:
    State* state = nullptr;

public:
    Doors(State* initial_state) {
        initial_state->set_context(this);
        state = initial_state;
        std::cout << "[" << state->get_name() << "]" << std::endl;
    }

    Doors(const Doors&) = delete;
    Doors& operator=(const Doors&) = delete;

    ~Doors() {
        if (state != nullptr) {
            delete state;
        }
    }

    void transition_to(State *new_state) {
        std::cout << state->get_name() << " ===> " << new_state->get_name() << std::endl;
        if (state != nullptr) {
            delete state;
        }
        new_state->set_context(this);
        state = new_state;
    }

    void open() {
        state->open();
    }

    void close() {
        state->close();
    }
};


class Closed : public State {
public:
    std::string get_name() override { return "Closed"; }
    void open() override;
};

class Open : public State {
public:
    std::string get_name() override { return "Open"; }
    void close() override;
};

// Closed
void Closed::open() {
    statemachine->transition_to(new Open);
}


// Open
void Open::close() {
    statemachine->transition_to(new Closed);
}


typedef void (Doors::*Event)();

int main() {
    Doors *statemachine = new Doors(new Closed);

    static std::map<std::string, Event> event_by_name;
    event_by_name["open"] = &Doors::open;
    event_by_name["close"] = &Doors::close;

    for (std::string input; std::getline(std::cin, input);) {
        std::map<std::string, Event>::const_iterator event_by_name_it = event_by_name.find(input);
        if (event_by_name_it == event_by_name.end()) {
            std::cout << "There is no event <" << input << "> in the Doors statemachine." << std::endl;
            continue;
        }
        Event event_invoker = event_by_name_it->second;
        (statemachine->*event_invoker)();
    }

    delete statemachine;
    return 0;
}
