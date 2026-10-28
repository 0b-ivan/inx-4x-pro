#pragma once

// Offline AWS glossary for the X4 Pro study reader. The glossary is curated
// device help, NOT copied certification-bank text and not fetched from GitHub.
// A hold in a page containing a known term opens the list of matching terms,
// so a user can choose the exact one without unreliable glyph-coordinate math.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace trivia {
struct GlossaryEntry {
  const char* term;   // shown in the list and as the detail title
  const char* match;  // whole-word token searched for; the short service name,
                      // so "IAM-Rolle" and "an S3 bucket" are recognised too
  const char* en;
  const char* de;
};
inline constexpr GlossaryEntry kAwsGlossary[] = {
  {"Amazon S3", "S3", "Object storage for files and other objects. Buckets contain objects; access is controlled by policies.", "Objektspeicher fuer Dateien und andere Objekte. Buckets enthalten Objekte; Richtlinien steuern den Zugriff."},
  {"Amazon EC2", "EC2", "Virtual servers in AWS. Choose instance sizes, networks and operating systems to run workloads.", "Virtuelle Server in AWS. Instanzgroesse, Netzwerk und Betriebssystem koennen gewaehlt werden."},
  {"AWS Lambda", "Lambda", "Runs event-driven code without provisioning servers; billed mainly for requests and execution.", "Fuehrt ereignisgesteuerten Code ohne eigene Serververwaltung aus; Abrechnung u.a. nach Ausfuehrung."},
  {"Amazon EKS", "EKS", "Managed Kubernetes control plane for running container workloads.", "Verwaltete Kubernetes-Steuerungsebene fuer Container-Workloads."},
  {"Amazon ECS", "ECS", "AWS container orchestrator for services and tasks, including Fargate-based workloads.", "AWS-Orchestrierung fuer Container-Dienste und Tasks, auch auf Fargate."},
  {"AWS Fargate", "Fargate", "Serverless compute engine for containers used by ECS and EKS.", "Serverlose Rechenkapazitaet fuer Container, die ECS und EKS nutzen koennen."},
  {"Amazon RDS", "RDS", "Managed relational database service with backups, maintenance and optional Multi-AZ availability.", "Verwalteter relationaler Datenbankdienst mit Backups, Wartung und optionalem Multi-AZ-Betrieb."},
  {"Amazon DynamoDB", "DynamoDB", "Fully managed NoSQL database with key-based access and scalable throughput.", "Vollstaendig verwaltete NoSQL-Datenbank mit schluesselbasiertem Zugriff und skalierbarem Durchsatz."},
  {"Amazon CloudWatch", "CloudWatch", "AWS monitoring service for metrics, logs, alarms and operational dashboards.", "AWS-Monitoring fuer Metriken, Logs, Alarme und Betriebsdashboards."},
  {"AWS CloudTrail", "CloudTrail", "Records AWS API and account activity for auditing and investigation.", "Protokolliert AWS-API- und Kontoaktivitaeten fuer Audits und Untersuchungen."},
  {"AWS CloudFormation", "CloudFormation", "Infrastructure as code service that provisions AWS resources from declarative templates.", "Infrastructure-as-Code-Dienst, der AWS-Ressourcen aus deklarativen Vorlagen bereitstellt."},
  {"AWS CodePipeline", "CodePipeline", "Continuous delivery service that connects source, build, test and deploy stages.", "Continuous-Delivery-Dienst fuer Source-, Build-, Test- und Deployment-Schritte."},
  {"AWS CodeBuild", "CodeBuild", "Managed build service for compiling code and running automated tests.", "Verwalteter Build-Dienst zum Kompilieren von Code und Ausfuehren automatisierter Tests."},
  {"AWS CodeDeploy", "CodeDeploy", "Deployment automation for EC2, Lambda and ECS applications.", "Automatisiert Deployments fuer EC2-, Lambda- und ECS-Anwendungen."},
  {"AWS IAM", "IAM", "Identity and Access Management controls permissions using users, roles and policies.", "Identity and Access Management steuert Berechtigungen durch Benutzer, Rollen und Richtlinien."},
  {"Amazon VPC", "VPC", "Logically isolated virtual network in AWS with subnets, routing and security controls.", "Logisch isoliertes virtuelles AWS-Netzwerk mit Subnetzen, Routing und Sicherheitsregeln."},
  {"Amazon Route 53", "Route 53", "Managed DNS and domain routing with optional health-check-based routing.", "Verwalteter DNS-Dienst mit optionalem Routing auf Basis von Health Checks."},
  {"Elastic Load Balancing", "Elastic Load Balancing", "Distributes incoming application or network traffic across targets.", "Verteilt eingehenden Anwendungs- oder Netzwerkverkehr auf Ziele."},
  {"AWS Systems Manager", "Systems Manager", "Operations service for inventory, automation, patching and managed node access.", "Betriebsdienst fuer Inventarisierung, Automatisierung, Patching und Zugriff auf verwaltete Knoten."},
  {"AWS Organizations", "AWS Organizations", "Central governance of multiple AWS accounts, including consolidated billing and service control policies.", "Zentrale Verwaltung mehrerer AWS-Konten, inklusive gemeinsamer Abrechnung und Service Control Policies."},
  {"AWS KMS", "KMS", "Managed service for creating and controlling cryptographic keys.", "Verwalteter Dienst zur Erstellung und Kontrolle kryptografischer Schluessel."},
  {"Amazon SQS", "SQS", "Managed message queue that decouples application components.", "Verwaltete Nachrichtenwarteschlange zur Entkopplung von Anwendungen."},
  {"Amazon SNS", "SNS", "Publish/subscribe messaging service for notifications and fan-out.", "Publish/Subscribe-Nachrichtendienst fuer Benachrichtigungen und Fan-out."},
  {"EventBridge", "EventBridge", "Event bus and routing service for connecting event sources with consumers.", "Event-Bus und Routing-Dienst zur Verbindung von Ereignisquellen und Empfaengern."},
  {"Auto Scaling", "Auto Scaling", "Adjusts compute capacity automatically according to demand or policy.", "Passt Rechenkapazitaet anhand von Nachfrage oder Regeln automatisch an."}
};
inline constexpr uint8_t kAwsGlossaryMaxMatches = 6;

inline bool awsAsciiWordChar(const unsigned char c) {
  return std::isalnum(c) || c == '_';
}
// Case-insensitive ASCII match with whole-term boundaries, so "IAM" cannot
// match part of another word. UTF-8 user text passes through unchanged.
inline bool awsContainsTerm(const char* text, const char* term) {
  if (!text || !term || !*term) return false;
  const size_t n = std::strlen(term);
  for (const char* p = text; *p; ++p) {
    size_t i = 0;
    for (; i < n && p[i]; ++i) {
      const unsigned char lhs = static_cast<unsigned char>(p[i]);
      const unsigned char rhs = static_cast<unsigned char>(term[i]);
      if (std::tolower(lhs) != std::tolower(rhs)) break;
    }
    if (i == n && (!awsAsciiWordChar(static_cast<unsigned char>(
                       p == text ? 0 : p[-1]))) &&
        !awsAsciiWordChar(static_cast<unsigned char>(p[n]))) return true;
  }
  return false;
}
inline uint8_t awsGlossaryMatches(const char* text, uint8_t (&out)[kAwsGlossaryMaxMatches]) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < sizeof(kAwsGlossary)/sizeof(kAwsGlossary[0]); ++i) {
    if (awsContainsTerm(text, kAwsGlossary[i].match)) {
      out[count++] = i;
      if (count == kAwsGlossaryMaxMatches) break;
    }
  }
  return count;
}
}  // namespace trivia
