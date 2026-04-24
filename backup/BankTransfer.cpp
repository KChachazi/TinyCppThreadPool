#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <random>

using namespace std;
const int TOTAL_THREADS = 10;
const int TOTAL_TRANSFERS_PER_THREAD = 100;

class BankAccount {
private:
    string accountHolder;
    double balance;
    mutex mtx;
public:
    BankAccount() = default;
    BankAccount(const string& holder, double initialBalance) : accountHolder(holder), balance(initialBalance) {}
    mutex& AcquireLock() {
        return mtx;
    }
    const string& GetAccountHolder() const {
        return accountHolder;
    }
    double GetBalance() const {
        return balance;
    }
    bool Deposit(double amount) {
        if (amount < 0) return false;
        balance += amount;
        return true;
    }
    bool Withdraw(double amount) {
        if (amount < 0 || amount > balance) return false;
        balance -= amount;
        return true;
    }
};
BankAccount account1("Alice", 1000);
BankAccount account2("Bob", 500);

void transfer(BankAccount& from, BankAccount& to, double amount) {
    scoped_lock lock(from.AcquireLock(), to.AcquireLock());

    if (from.Withdraw(amount)) {
        to.Deposit(amount);
        cout << "Transferred " << amount << " from " << from.GetAccountHolder() << " to " << to.GetAccountHolder() << "." << endl;
    } else {
        cout << "Failed to transfer " << amount << " from " << from.GetAccountHolder() << " to " << to.GetAccountHolder() << "." << endl;
    }
}

void SnapshotTotal() {
    unique_lock<mutex> lock1(account1.AcquireLock(), defer_lock);
    unique_lock<mutex> lock2(account2.AcquireLock(), defer_lock);

    // Lock both deferred locks together to avoid deadlock.
    std::lock(lock1, lock2);
    double total = account1.GetBalance() + account2.GetBalance();
    cout << "Total balance: " << total << endl;
    cout << "Account 1: " << account1.GetBalance() << ", Account 2: " << account2.GetBalance() << endl;
}

void MonitorTask(atomic<bool>& stopRequested) {
    while (!stopRequested.load()) {
        this_thread::sleep_for(chrono::milliseconds(20));
        SnapshotTotal();
    }
}

int main() {
    atomic<bool> stopMonitor(false);
    thread monitor(MonitorTask, ref(stopMonitor));

    thread threads[TOTAL_THREADS];
    for (int i = 0; i < TOTAL_THREADS; ++i) {
        threads[i] = thread([&]() {
            // Keep RNG local to each worker thread; avoid sharing rand().
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> amountDist(1, 100);
            std::uniform_int_distribution<int> sideDist(0, 1);

            for (int j = 0; j < TOTAL_TRANSFERS_PER_THREAD; ++j) {
                double amount = static_cast<double>(amountDist(gen));
                if (sideDist(gen) == 1) {
                    transfer(account1, account2, amount);
                } else {
                    transfer(account2, account1, amount);
                }
            }
        });
    }

    for (int i = 0; i < TOTAL_THREADS; ++i) {
        threads[i].join();
    }

    // Stop monitor after workers finish, then join for clean shutdown.
    stopMonitor.store(true);
    monitor.join();

    double total = account1.GetBalance() + account2.GetBalance();
    cout << "Final balances:" << endl;
    cout << "Account 1: " << account1.GetBalance() << endl;
    cout << "Account 2: " << account2.GetBalance() << endl;
    cout << "Final total: " << total << endl;

    return 0;
}