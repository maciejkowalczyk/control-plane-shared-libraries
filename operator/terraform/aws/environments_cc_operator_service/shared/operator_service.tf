/**
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

########################
# DO NOT EDIT MANUALLY #
########################

# This file is meant to be shared across all environments (either copied or
# symlinked). In order to make the upgrade process easier, this file should not
# be modified for environment-specific customization.

module "operator_service" {
  source = "../../applications/cc-operator-service"

  region      = var.region
  environment = var.environment

  instance_type = var.instance_type
  ami_name      = var.ami_name
  ami_owners    = var.ami_owners

  enclave_cpu_count  = var.enclave_cpu_count
  enclave_memory_mib = var.enclave_memory_mib

  coordinator_a_assume_role_parameter = var.coordinator_a_assume_role_parameter
  coordinator_b_assume_role_parameter = var.coordinator_b_assume_role_parameter


  initial_capacity_ec2_instances = var.initial_capacity_ec2_instances
  min_capacity_ec2_instances     = var.min_capacity_ec2_instances
  max_capacity_ec2_instances     = var.max_capacity_ec2_instances

  alarm_notification_email = var.alarm_notification_email

  # JobDb
  job_db_read_capacity  = var.job_db_read_capacity
  job_db_write_capacity = var.job_db_write_capacity

  # Worker Alarms
  worker_alarms_enabled            = var.worker_alarms_enabled
  job_client_error_threshold       = var.job_client_error_threshold
  job_validation_failure_threshold = var.job_validation_failure_threshold
  worker_job_error_threshold       = var.worker_job_error_threshold
  worker_alarm_eval_period_sec     = var.worker_alarm_eval_period_sec
  worker_alarm_metric_dimensions   = var.worker_alarm_metric_dimensions

  # Autoscaling Alarms
  asg_max_instances_alarm_ratio     = var.asg_max_instances_alarm_ratio
  autoscaling_alarm_eval_period_sec = var.autoscaling_alarm_eval_period_sec

  # Job Queue Alarms
  job_queue_old_message_threshold_sec = var.job_queue_old_message_threshold_sec
  job_queue_alarm_eval_period_sec     = var.job_queue_alarm_eval_period_sec

  # JobDb Alarms
  job_db_read_capacity_usage_ratio_alarm_threshold  = var.job_db_read_capacity_usage_ratio_alarm_threshold
  job_db_write_capacity_usage_ratio_alarm_threshold = var.job_db_write_capacity_usage_ratio_alarm_threshold
  job_db_alarm_eval_period_sec                      = var.job_db_alarm_eval_period_sec

  # VPC
  enable_user_provided_vpc             = var.enable_user_provided_vpc
  user_provided_vpc_security_group_ids = var.user_provided_vpc_security_group_ids
  user_provided_vpc_subnet_ids         = var.user_provided_vpc_subnet_ids
  vpc_cidr                             = var.vpc_cidr
  vpc_availability_zones               = var.vpc_availability_zones
}
